/*
 * PianoRoll.cpp - implementation of piano roll which is used for actual
 *                  writing of melodies
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2008 Andrew Kelley <superjoe30/at/gmail/dot/com>
 *
 * This file is part of LMMS - http://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMdiArea>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOption>
#include <QWheelEvent>
#include <QString>
#include <QSignalMapper>

#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif

#include <math.h>
#include <algorithm>

#include "AutomationEditor.h"
#include "ActionGroup.h"
#include "ConfigManager.h"
#include "PianoRoll.h"
#include "BBTrackContainer.h"
#include "Clipboard.h"
#include "ComboBox.h"
#include "debug.h"
#include "DetuningHelper.h"
#include "embed.h"
#include "GuiApplication.h"
#include "gui_templates.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiEvent.h"
#include "DataFile.h"
#include "Pattern.h"
#include "Piano.h"
#include "PixmapButton.h"
#include "Song.h"
#include "SongEditor.h"
#include "templates.h"
#include "TextFloat.h"
#include "TimeLineWidget.h"
#include "TextFloat.h"


#if QT_VERSION < 0x040800
#define MiddleButton MidButton
#endif


typedef AutomationPattern::timeMap timeMap;


// some constants...
const int INITIAL_PIANOROLL_HEIGHT = 480;

const int SCROLLBAR_SIZE = 16;
const int PIANO_X = 0;

const int WHITE_KEY_WIDTH = 64;
const int BLACK_KEY_WIDTH = 41;
const int WHITE_KEY_SMALL_HEIGHT = 18;
const int WHITE_KEY_BIG_HEIGHT = 24;
const int BLACK_KEY_HEIGHT = 16;
const int C_KEY_LABEL_X = WHITE_KEY_WIDTH - 19;
const int KEY_LINE_HEIGHT = 12;
const int OCTAVE_HEIGHT = KEY_LINE_HEIGHT * KeysPerOctave;	// = 12 * 12;

const int NOTE_EDIT_RESIZE_BAR = 6;
const int NOTE_EDIT_MIN_HEIGHT = 50;
const int KEY_AREA_MIN_HEIGHT = 100;
const int PR_BOTTOM_MARGIN = SCROLLBAR_SIZE;
const int PR_TOP_MARGIN = 16;
const int PR_RIGHT_MARGIN = SCROLLBAR_SIZE;


// width of area used for resizing (the grip at the end of a note)
const int RESIZE_AREA_WIDTH = 4;

// width of line for setting volume/panning of note
const int NOTE_EDIT_LINE_WIDTH = 3;

// key where to start
const int INITIAL_START_KEY = Key_C + Octave_4 * KeysPerOctave;

// number of each note to provide in quantization and note lengths
const int NUM_EVEN_LENGTHS = 6;
const int NUM_TRIPLET_LENGTHS = 5;



QPixmap * PianoRoll::s_whiteKeySmallPm = NULL;
QPixmap * PianoRoll::s_whiteKeySmallPressedPm = NULL;
QPixmap * PianoRoll::s_whiteKeyBigPm = NULL;
QPixmap * PianoRoll::s_whiteKeyBigPressedPm = NULL;
QPixmap * PianoRoll::s_blackKeyPm = NULL;
QPixmap * PianoRoll::s_blackKeyPressedPm = NULL;
QPixmap * PianoRoll::s_toolDraw = NULL;
QPixmap * PianoRoll::s_toolErase = NULL;
QPixmap * PianoRoll::s_toolSelect = NULL;
QPixmap * PianoRoll::s_toolMove = NULL;
QPixmap * PianoRoll::s_toolOpen = NULL;

TextFloat * PianoRoll::s_textFloat = NULL;

// used for drawing of piano
PianoRoll::PianoRollKeyTypes PianoRoll::prKeyOrder[] =
{
	PR_WHITE_KEY_SMALL, PR_BLACK_KEY, PR_WHITE_KEY_BIG, PR_BLACK_KEY,
	PR_WHITE_KEY_SMALL, PR_WHITE_KEY_SMALL, PR_BLACK_KEY, PR_WHITE_KEY_BIG,
	PR_BLACK_KEY, PR_WHITE_KEY_BIG, PR_BLACK_KEY, PR_WHITE_KEY_SMALL
} ;


const int DEFAULT_PR_PPT = KEY_LINE_HEIGHT * DefaultStepsPerTact;


PianoRoll::PianoRoll() :
	m_nemStr( QVector<QString>() ),
	m_noteEditMenu( NULL ),
	m_semiToneMarkerMenu( NULL ),
	m_zoomingModel(),
	m_quantizeModel(),
	m_noteLenModel(),
	m_pattern( NULL ),
	m_currentPosition(),
	m_recording( false ),
	m_currentNote( NULL ),
	m_action( ActionNone ),
	m_noteEditMode( NoteEditVolume ),
	m_moveBoundaryLeft( 0 ),
	m_moveBoundaryTop( 0 ),
	m_moveBoundaryRight( 0 ),
	m_moveBoundaryBottom( 0 ),
	m_mouseDownKey( 0 ),
	m_mouseDownTick( 0 ),
	m_lastMouseX( 0 ),
	m_lastMouseY( 0 ),
	m_oldNotesEditHeight( 100 ),
	m_notesEditHeight( 100 ),
	m_ppt( DEFAULT_PR_PPT ),
	m_lenOfNewNotes( MidiTime( 0, DefaultTicksPerTact/4 ) ),
	m_lastNoteVolume( DefaultVolume ),
	m_lastNotePanning( DefaultPanning ),
	m_startKey( INITIAL_START_KEY ),
	m_lastKey( 0 ),
	m_editMode( ModeDraw ),
	m_mouseDownLeft( false ),
	m_mouseDownRight( false ),
	m_scrollBack( false ),
	m_gridColor( 0, 0, 0 ),
	m_noteModeColor( 0, 0, 0 ),
	m_noteColor( 0, 0, 0 ),
	m_barColor( 0, 0, 0 )
{
	// gui names of edit modes
	m_nemStr.push_back( tr( "Note Volume" ) );
	m_nemStr.push_back( tr( "Note Panning" ) );

	QSignalMapper * signalMapper = new QSignalMapper( this );
	m_noteEditMenu = new QMenu( this );
	m_noteEditMenu->clear();
	for( int i = 0; i < m_nemStr.size(); ++i )
	{
		QAction * act = new QAction( m_nemStr.at(i), this );
		connect( act, SIGNAL(triggered()), signalMapper, SLOT(map()) );
		signalMapper->setMapping( act, i );
		m_noteEditMenu->addAction( act );
	}
	connect( signalMapper, SIGNAL(mapped(int)),
			this, SLOT(changeNoteEditMode(int)) );

	signalMapper = new QSignalMapper( this );
	m_semiToneMarkerMenu = new QMenu( this );

	QAction* markSemitoneAction = new QAction( tr("Mark/unmark current semitone"), this );
	QAction* markScaleAction = new QAction( tr("Mark current scale"), this );
	QAction* markChordAction = new QAction( tr("Mark current chord"), this );
	QAction* unmarkAllAction = new QAction( tr("Unmark all"), this );

	connect( markSemitoneAction, SIGNAL(triggered()), signalMapper, SLOT(map()) );
	connect( markScaleAction, SIGNAL(triggered()), signalMapper, SLOT(map()) );
	connect( markChordAction, SIGNAL(triggered()), signalMapper, SLOT(map()) );
	connect( unmarkAllAction, SIGNAL(triggered()), signalMapper, SLOT(map()) );

	signalMapper->setMapping( markSemitoneAction, static_cast<int>( stmaMarkCurrentSemiTone ) );
	signalMapper->setMapping( markScaleAction, static_cast<int>( stmaMarkCurrentScale ) );
	signalMapper->setMapping( markChordAction, static_cast<int>( stmaMarkCurrentChord ) );
	signalMapper->setMapping( unmarkAllAction, static_cast<int>( stmaUnmarkAll ) );

	markScaleAction->setEnabled( false );
	markChordAction->setEnabled( false );

	connect( this, SIGNAL(semiToneMarkerMenuScaleSetEnabled(bool)), markScaleAction, SLOT(setEnabled(bool)) );
	connect( this, SIGNAL(semiToneMarkerMenuChordSetEnabled(bool)), markChordAction, SLOT(setEnabled(bool)) );

	connect( signalMapper, SIGNAL(mapped(int)), this, SLOT(markSemiTone(int)) );

	m_semiToneMarkerMenu->addAction( markSemitoneAction );
	m_semiToneMarkerMenu->addAction( markScaleAction );
	m_semiToneMarkerMenu->addAction( markChordAction );
	m_semiToneMarkerMenu->addAction( unmarkAllAction );

	// init pixmaps
	if( s_whiteKeySmallPm == NULL )
	{
		s_whiteKeySmallPm = new QPixmap( embed::getIconPixmap(
						"pr_white_key_small" ) );
	}
	if( s_whiteKeySmallPressedPm == NULL )
	{
		s_whiteKeySmallPressedPm = new QPixmap( embed::getIconPixmap(
						"pr_white_key_small_pressed" ) );
	}
	if( s_whiteKeyBigPm == NULL )
	{
		s_whiteKeyBigPm = new QPixmap( embed::getIconPixmap(
							"pr_white_key_big" ) );
	}
	if( s_whiteKeyBigPressedPm == NULL )
	{
		s_whiteKeyBigPressedPm = new QPixmap( embed::getIconPixmap(
							"pr_white_key_big_pressed" ) );
	}
	if( s_blackKeyPm == NULL )
	{
		s_blackKeyPm = new QPixmap( embed::getIconPixmap(
							"pr_black_key" ) );
	}
	if( s_blackKeyPressedPm == NULL )
	{
		s_blackKeyPressedPm = new QPixmap( embed::getIconPixmap(
							"pr_black_key_pressed" ) );
	}
	if( s_toolDraw == NULL )
	{
		s_toolDraw = new QPixmap( embed::getIconPixmap( "edit_draw" ) );
	}
	if( s_toolErase == NULL )
	{
		s_toolErase= new QPixmap( embed::getIconPixmap( "edit_erase" ) );
	}
	if( s_toolSelect == NULL )
	{
		s_toolSelect = new QPixmap( embed::getIconPixmap( "edit_select" ) );
	}
	if( s_toolMove == NULL )
	{
		s_toolMove = new QPixmap( embed::getIconPixmap( "edit_move" ) );
	}
	if( s_toolOpen == NULL )
	{
		s_toolOpen = new QPixmap( embed::getIconPixmap( "automation" ) );
	}

	// init text-float
	if( s_textFloat == NULL )
	{
		s_textFloat = new TextFloat;
	}

	setAttribute( Qt::WA_OpaquePaintEvent, true );

	// add time-line
	m_timeLine = new TimeLineWidget( WHITE_KEY_WIDTH, 0, m_ppt,
					Engine::getSong()->getPlayPos(
						Song::Mode_PlayPattern ),
						m_currentPosition, this );
	connect( this, SIGNAL( positionChanged( const MidiTime & ) ),
		m_timeLine, SLOT( updatePosition( const MidiTime & ) ) );
	connect( m_timeLine, SIGNAL( positionChanged( const MidiTime & ) ),
			this, SLOT( updatePosition( const MidiTime & ) ) );

	// update timeline when in record-accompany mode
	connect( Engine::getSong()->getPlayPos( Song::Mode_PlaySong ).m_timeLine,
				SIGNAL( positionChanged( const MidiTime & ) ),
			this,
			SLOT( updatePositionAccompany( const MidiTime & ) ) );
	// TODO
/*	connect( engine::getSong()->getPlayPos( Song::Mode_PlayBB ).m_timeLine,
				SIGNAL( positionChanged( const MidiTime & ) ),
			this,
			SLOT( updatePositionAccompany( const MidiTime & ) ) );*/

	removeSelection();

	// init scrollbars
	m_leftRightScroll = new QScrollBar( Qt::Horizontal, this );
	m_leftRightScroll->setSingleStep( 1 );
	connect( m_leftRightScroll, SIGNAL( valueChanged( int ) ), this,
						SLOT( horScrolled( int ) ) );

	m_topBottomScroll = new QScrollBar( Qt::Vertical, this );
	m_topBottomScroll->setSingleStep( 1 );
	m_topBottomScroll->setPageStep( 20 );
	connect( m_topBottomScroll, SIGNAL( valueChanged( int ) ), this,
						SLOT( verScrolled( int ) ) );

	// setup zooming-stuff
	for( int i = 0; i < 6; ++i )
	{
		m_zoomingModel.addItem( QString::number( 25 << i ) + "%" );
	}
	m_zoomingModel.setValue( m_zoomingModel.findText( "100%" ) );
	connect( &m_zoomingModel, SIGNAL( dataChanged() ),
					this, SLOT( zoomingChanged() ) );

	// Set up quantization model
	m_quantizeModel.addItem( tr( "Note lock" ) );
	for( int i = 0; i <= NUM_EVEN_LENGTHS; ++i )
	{
		m_quantizeModel.addItem( "1/" + QString::number( 1 << i ) );
	}
	for( int i = 0; i < NUM_TRIPLET_LENGTHS; ++i )
	{
		m_quantizeModel.addItem( "1/" + QString::number( (1 << i) * 3 ) );
	}
	m_quantizeModel.addItem( "1/192" );
	m_quantizeModel.setValue( m_quantizeModel.findText( "1/16" ) );

	connect( &m_quantizeModel, SIGNAL( dataChanged() ),
					this, SLOT( quantizeChanged() ) );

	// Set up note length model
	m_noteLenModel.addItem( tr( "Last note" ),
					new PixmapLoader( "edit_draw" ) );
	const QString pixmaps[] = { "whole", "half", "quarter", "eighth",
						"sixteenth", "thirtysecond", "triplethalf",
						"tripletquarter", "tripleteighth",
						"tripletsixteenth", "tripletthirtysecond" } ;

	for( int i = 0; i < NUM_EVEN_LENGTHS; ++i )
	{
		PixmapLoader *loader = new PixmapLoader( "note_" + pixmaps[i] );
		m_noteLenModel.addItem( "1/" + QString::number( 1 << i ), loader );
	}
	for( int i = 0; i < NUM_TRIPLET_LENGTHS; ++i )
	{
		PixmapLoader *loader = new PixmapLoader( "note_" + pixmaps[i+NUM_EVEN_LENGTHS] );
		m_noteLenModel.addItem( "1/" + QString::number( (1 << i) * 3 ), loader );
	}
	m_noteLenModel.setValue( 0 );

	// Note length change can cause a redraw if Q is set to lock
	connect( &m_noteLenModel, SIGNAL( dataChanged() ),
					this, SLOT( quantizeChanged() ) );

	// Set up scale model
	const auto& chord_table = InstrumentFunctionNoteStacking::ChordTable::getInstance();

	m_scaleModel.addItem( tr("No scale") );
	for( const InstrumentFunctionNoteStacking::Chord& chord : chord_table )
	{
		if( chord.isScale() )
		{
			m_scaleModel.addItem( chord.getName() );
		}
	}

	m_scaleModel.setValue( 0 );
	// change can update m_semiToneMarkerMenu
	connect( &m_scaleModel, SIGNAL( dataChanged() ),
						this, SLOT( updateSemiToneMarkerMenu() ) );

	// Set up chord model
	m_chordModel.addItem( tr("No chord") );
	for( const InstrumentFunctionNoteStacking::Chord& chord : chord_table )
	{
		if( ! chord.isScale() )
		{
			m_chordModel.addItem( chord.getName() );
		}
	}

	m_chordModel.setValue( 0 );

	// change can update m_semiToneMarkerMenu
	connect( &m_chordModel, SIGNAL( dataChanged() ),
					this, SLOT( updateSemiToneMarkerMenu() ) );

	setFocusPolicy( Qt::StrongFocus );
	setFocus();
	setMouseTracking( true );

	connect( &m_scaleModel, SIGNAL( dataChanged() ),
					this, SLOT( updateSemiToneMarkerMenu() ) );

	connect( Engine::getSong(), SIGNAL( timeSignatureChanged( int, int ) ),
						this, SLOT( update() ) );
}



void PianoRoll::reset()
{
	m_lastNoteVolume = DefaultVolume;
	m_lastNotePanning = DefaultPanning;
}



void PianoRoll::changeNoteEditMode( int i )
{
	m_noteEditMode = (NoteEditMode) i;
	repaint();
}


void PianoRoll::markSemiTone( int i )
{
	const int key = getKey( mapFromGlobal( m_semiToneMarkerMenu->pos() ).y() );
	const InstrumentFunctionNoteStacking::Chord * chord = nullptr;

	switch( static_cast<SemiToneMarkerAction>( i ) )
	{
		case stmaUnmarkAll:
			m_markedSemiTones.clear();
			break;
		case stmaMarkCurrentSemiTone:
		{
			QList<int>::iterator it = qFind( m_markedSemiTones.begin(), m_markedSemiTones.end(), key );
			if( it != m_markedSemiTones.end() )
			{
				m_markedSemiTones.erase( it );
			}
			else
			{
				m_markedSemiTones.push_back( key );
			}
			break;
		}
		case stmaMarkCurrentScale:
			chord = & InstrumentFunctionNoteStacking::ChordTable::getInstance()
					.getScaleByName( m_scaleModel.currentText() );
		case stmaMarkCurrentChord:
		{
			if( ! chord )
			{
				chord = & InstrumentFunctionNoteStacking::ChordTable::getInstance()
						.getChordByName( m_chordModel.currentText() );
			}

			if( chord->isEmpty() )
			{
				break;
			}
			else if( chord->isScale() )
			{
				m_markedSemiTones.clear();
			}

			const int first = chord->isScale() ? 0 : key;
			const int last = chord->isScale() ? NumKeys : key + chord->last();
			const int cap = ( chord->isScale() || chord->last() == 0 ) ? KeysPerOctave : chord->last();

			for( int i = first; i <= last; i++ )
			{
			  if( chord->hasSemiTone( ( i + cap - ( key % cap ) ) % cap ) )
				{
					m_markedSemiTones.push_back( i );
				}
			}
			break;
		}
		default:
			;
	}

	qSort( m_markedSemiTones.begin(), m_markedSemiTones.end(), qGreater<int>() );
	QList<int>::iterator new_end = std::unique( m_markedSemiTones.begin(), m_markedSemiTones.end() );
	m_markedSemiTones.erase( new_end, m_markedSemiTones.end() );
}


PianoRoll::~PianoRoll()
{
}


void PianoRoll::setCurrentPattern( Pattern* newPattern )
{
	if( hasValidPattern() )
	{
		m_pattern->instrumentTrack()->disconnect( this );
	}

	// force the song-editor to stop playing if it played pattern before
	if( Engine::getSong()->isPlaying() &&
		Engine::getSong()->playMode() == Song::Mode_PlayPattern )
	{
		Engine::getSong()->playPattern( NULL );
	}

	// set new data
	m_pattern = newPattern;
	m_currentPosition = 0;
	m_currentNote = NULL;
	m_startKey = INITIAL_START_KEY;

	if( ! hasValidPattern() )
	{
		//resizeEvent( NULL );
		setWindowTitle( tr( "Piano-Roll - no pattern" ) );

		update();
		emit currentPatternChanged();
		return;
	}

	m_leftRightScroll->setValue( 0 );

	const NoteVector & notes = m_pattern->notes();
	int central_key = 0;
	if( ! notes.empty() )
	{
		// determine the central key so that we can scroll to it
		int total_notes = 0;
		for( const Note* const& note : notes )
		{
			if( note->length() > 0 )
			{
				central_key += note->key();
				++total_notes;
			}
		}

		if( total_notes > 0 )
		{
			central_key = central_key / total_notes -
					( KeysPerOctave * NumOctaves - m_totalKeysToScroll ) / 2;
			m_startKey = tLimit( central_key, 0, NumOctaves * KeysPerOctave );
		}
	}

	// resizeEvent() does the rest for us (scrolling, range-checking
	// of start-notes and so on...)
	resizeEvent( NULL );

	// make sure to always get informed about the pattern being destroyed
	connect( m_pattern, SIGNAL( destroyedPattern( Pattern* ) ), this, SLOT( hidePattern( Pattern* ) ) );

	connect( m_pattern->instrumentTrack(), SIGNAL( midiNoteOn( const Note& ) ), this, SLOT( startRecordNote( const Note& ) ) );
	connect( m_pattern->instrumentTrack(), SIGNAL( midiNoteOff( const Note& ) ), this, SLOT( finishRecordNote( const Note& ) ) );
	connect( m_pattern->instrumentTrack()->pianoModel(), SIGNAL( dataChanged() ), this, SLOT( update() ) );

	setWindowTitle( tr( "Piano-Roll - %1" ).arg( m_pattern->name() ) );

	update();
	emit currentPatternChanged();
}



void PianoRoll::hidePattern( Pattern* pattern )
{
	if( m_pattern == pattern )
	{
		setCurrentPattern( NULL );
	}
}




/** \brief qproperty access implementation */

QColor PianoRoll::gridColor() const
{ return m_gridColor; }

void PianoRoll::setGridColor( const QColor & c )
{ m_gridColor = c; }

QColor PianoRoll::noteModeColor() const
{ return m_noteModeColor; }

void PianoRoll::setNoteModeColor( const QColor & c )
{ m_noteModeColor = c; }

QColor PianoRoll::noteColor() const
{ return m_noteColor; }

void PianoRoll::setNoteColor( const QColor & c )
{ m_noteColor = c; }

QColor PianoRoll::barColor() const
{ return m_barColor; }

void PianoRoll::setBarColor( const QColor & c )
{ m_barColor = c; }



inline void PianoRoll::drawNoteRect(QPainter & p, int x, int y,
					int width, Note * n, const QColor & noteCol )
{
	++x;
	++y;
	width -= 2;

	if( width <= 0 )
	{
		width = 2;
	}

	int volVal = qMin( 255, 25 + (int) ( ( (float)( n->getVolume() - MinVolume ) ) /
			( (float)( MaxVolume - MinVolume ) ) * 230.0f) );
	float rightPercent = qMin<float>( 1.0f,
			( (float)( n->getPanning() - PanningLeft ) ) /
			( (float)( PanningRight - PanningLeft ) ) * 2.0f );

	float leftPercent = qMin<float>( 1.0f,
			( (float)( PanningRight - n->getPanning() ) ) /
			( (float)( PanningRight - PanningLeft ) ) * 2.0f );

	QColor col = QColor( noteCol );

	if( n->length() < 0 )
	{
		//step note
		col.setRgb( 0, 255, 0 );
	}
	else if( n->selected() )
	{
		col.setRgb( 0x00, 0x40, 0xC0 );
	}

	// adjust note to make it a bit faded if it has a lower volume
	// in stereo using gradients
	QColor lcol = QColor::fromHsv( col.hue(), col.saturation(),
						volVal * leftPercent );
	QColor rcol = QColor::fromHsv( col.hue(), col.saturation(),
						volVal * rightPercent );
	col = QColor::fromHsv( col.hue(), col.saturation(), volVal );

	QLinearGradient gradient( x, y, x+width,
						y+KEY_LINE_HEIGHT );
	gradient.setColorAt( 0, lcol );
	gradient.setColorAt( 1, rcol );
	p.setBrush( gradient );
	p.setPen( QColor::fromHsv( col.hue(), col.saturation(),
					qMin<float>( 255, volVal*1.7f ) ) );
	p.setRenderHint(QPainter::Antialiasing);
	p.drawRoundedRect( x, y, width, KEY_LINE_HEIGHT-1, 5, 2 );

	// that little tab thing on the end hinting at the user
	// to resize the note
	p.setPen( noteCol.lighter( 200 ) );
	if( width > 2 )
	{
		p.drawLine( x + width - 3, y + 2, x + width - 3,
						y + KEY_LINE_HEIGHT - 4 );
	}
	p.drawLine( x + width - 1, y + 2, x + width - 1,
						y + KEY_LINE_HEIGHT - 4 );
	p.drawLine( x + width - 2, y + 2, x + width - 2,
						y + KEY_LINE_HEIGHT - 4 );
}




inline void PianoRoll::drawDetuningInfo( QPainter & _p, Note * _n, int _x,
								int _y )
{
	int middle_y = _y + KEY_LINE_HEIGHT / 2;
	_p.setPen( noteColor() );

	int old_x = 0;
	int old_y = 0;

	timeMap & map = _n->detuning()->automationPattern()->getTimeMap();
	for( timeMap::ConstIterator it = map.begin(); it != map.end(); ++it )
	{
		int pos_ticks = it.key();
		if( pos_ticks > _n->length() )
		{
			break;
		}
		int pos_x = _x + pos_ticks * m_ppt / MidiTime::ticksPerTact();

		const float level = it.value();

		int pos_y = middle_y - level * KEY_LINE_HEIGHT;

		if( old_x != 0 && old_y != 0 )
		{
			switch( _n->detuning()->automationPattern()->progressionType() )
			{
			case AutomationPattern::DiscreteProgression:
				_p.drawLine( old_x, old_y, pos_x, old_y );
				_p.drawLine( pos_x, old_y, pos_x, pos_y );
				break;
			case AutomationPattern::CubicHermiteProgression: /* TODO */
			case AutomationPattern::LinearProgression:
				_p.drawLine( old_x, old_y, pos_x, pos_y );
				break;
			}
		}

		_p.drawLine( pos_x - 1, pos_y, pos_x + 1, pos_y );
		_p.drawLine( pos_x, pos_y - 1, pos_x, pos_y + 1 );

		old_x = pos_x;
		old_y = pos_y;
	}
}




void PianoRoll::removeSelection()
{
	m_selectStartTick = 0;
	m_selectedTick = 0;
	m_selectStartKey = 0;
	m_selectedKeys = 0;
}




void PianoRoll::clearSelectedNotes()
{
	if( m_pattern != NULL )
	{
		// get note-vector of current pattern
		const NoteVector & notes = m_pattern->notes();

		// will be our iterator in the following loop
		NoteVector::ConstIterator it;
		for( it = notes.begin(); it != notes.end(); ++it ) {
			Note *note = *it;
			note->setSelected( false );
		}
	}
}




void PianoRoll::shiftSemiTone( int amount ) // shift notes by amount semitones
{
	bool useAllNotes = ! isSelection();
	const NoteVector & notes = m_pattern->notes();
	NoteVector::ConstIterator it;
	for( it = notes.begin(); it != notes.end(); ++it )
	{
		Note *note = *it;
		// if none are selected, move all notes, otherwise
		// only move selected notes
		if( useAllNotes || note->selected() )
		{
			note->setKey( note->key() + amount );
		}
	}

	// we modified the song
	update();
	gui->songEditor()->update();
}




void PianoRoll::shiftPos( int amount ) //shift notes pos by amount
{
	bool useAllNotes = ! isSelection();
	const NoteVector & notes = m_pattern->notes();
	NoteVector::ConstIterator it;

	bool first = true;
	for( it = notes.begin(); it != notes.end(); ++it )
	{
		Note *note = *it;
		// if none are selected, move all notes, otherwise
		// only move selected notes
		if( note->selected() || (useAllNotes && note->length() > 0) )
		{
			// don't let notes go to out of bounds
			if( first )
			{
				m_moveBoundaryLeft = note->pos();
				if( m_moveBoundaryLeft + amount < 0 )
				{
					amount += 0 - (amount + m_moveBoundaryLeft);
				}
				first = false;
			}
			note->setPos( note->pos() + amount );
		}
	}

	// we modified the song
	update();
	gui->songEditor()->update();
}




bool PianoRoll::isSelection() const // are any notes selected?
{
	const NoteVector & notes = m_pattern->notes();
	NoteVector::ConstIterator it;
	for( it = notes.begin(); it != notes.end(); ++it )
	{
		Note *note = *it;
		if( note->selected() )
		{
			return true;
		}
	}

	return false;
}



int PianoRoll::selectionCount() const // how many notes are selected?
{
	int sum = 0;

	const NoteVector & notes = m_pattern->notes();
	NoteVector::ConstIterator it;
	for( it = notes.begin(); it != notes.end(); ++it )
	{
		Note *note = *it;
		if( note->selected() )
		{
			++sum;
		}
	}

	return sum;
}



void PianoRoll::keyPressEvent(QKeyEvent* ke )
{
	if( hasValidPattern() && ke->modifiers() == Qt::NoModifier )
	{
		const int key_num = PianoView::getKeyFromKeyEvent( ke ) + ( DefaultOctave - 1 ) * KeysPerOctave;

		if(! ke->isAutoRepeat() && key_num > -1)
		{
			m_pattern->instrumentTrack()->pianoModel()->handleKeyPress( key_num );
			ke->accept();
		}
	}

	switch( ke->key() )
	{
		case Qt::Key_Up:
			if( ( ke->modifiers() & Qt::ControlModifier ) && m_action == ActionNone )
			{
				// shift selection up an octave
				// if nothing selected, shift _everything_
				shiftSemiTone( +12 );
			}
			else if((ke->modifiers() & Qt::ShiftModifier) && m_action == ActionNone)
			{
				// Move selected notes up by one semitone
				shiftSemiTone( 1 );
			}
			else
			{
				// scroll
				m_topBottomScroll->setValue(
						m_topBottomScroll->value() -
							cm_scrollAmtVert );

				// if they are moving notes around or resizing,
				// recalculate the note/resize position
				if( m_action == ActionMoveNote ||
						m_action == ActionResizeNote )
				{
					dragNotes( m_lastMouseX, m_lastMouseY,
								ke->modifiers() & Qt::AltModifier,
								ke->modifiers() & Qt::ShiftModifier );
				}
			}
			ke->accept();
			break;

		case Qt::Key_Down:
			if( ke->modifiers() & Qt::ControlModifier && m_action == ActionNone )
			{
				// shift selection down an octave
				// if nothing selected, shift _everything_
				shiftSemiTone( -12 );
			}
			else if((ke->modifiers() & Qt::ShiftModifier) && m_action == ActionNone)
			{
				// Move selected notes down by one semitone
				shiftSemiTone( -1 );
			}
			else
			{
				// scroll
				m_topBottomScroll->setValue(
						m_topBottomScroll->value() +
							cm_scrollAmtVert );

				// if they are moving notes around or resizing,
				// recalculate the note/resize position
				if( m_action == ActionMoveNote ||
						m_action == ActionResizeNote )
				{
					dragNotes( m_lastMouseX, m_lastMouseY,
								ke->modifiers() & Qt::AltModifier,
								ke->modifiers() & Qt::ShiftModifier );
				}
			}
			ke->accept();
			break;

		case Qt::Key_Left:
			if( ke->modifiers() & Qt::ControlModifier && m_action == ActionNone )
			{
				// Move selected notes by one bar to the left
				shiftPos( - MidiTime::ticksPerTact() );
			}
			else if( ke->modifiers() & Qt::ShiftModifier && m_action == ActionNone)
			{
				// move notes
				bool quantized = ! ( ke->modifiers() & Qt::AltModifier );
				int amt = quantized ? quantization() : 1;
				shiftPos( -amt );
			}
			else if( ke->modifiers() & Qt::AltModifier)
			{
				Pattern * p = m_pattern->previousPattern();
				if(p != NULL)
				{
					setCurrentPattern(p);
				}
			}
			else
			{
				// scroll
				m_leftRightScroll->setValue(
						m_leftRightScroll->value() -
							cm_scrollAmtHoriz );

				// if they are moving notes around or resizing,
				// recalculate the note/resize position
				if( m_action == ActionMoveNote ||
						m_action == ActionResizeNote )
				{
					dragNotes( m_lastMouseX, m_lastMouseY,
								ke->modifiers() & Qt::AltModifier,
								ke->modifiers() & Qt::ShiftModifier );
				}

			}
			ke->accept();
			break;

		case Qt::Key_Right:
			if( ke->modifiers() & Qt::ControlModifier && m_action == ActionNone)
			{
				// Move selected notes by one bar to the right
				shiftPos( MidiTime::ticksPerTact() );
			}
			else if( ke->modifiers() & Qt::ShiftModifier && m_action == ActionNone)
			{
				// move notes
				bool quantized = !( ke->modifiers() & Qt::AltModifier );
				int amt = quantized ? quantization() : 1;
				shiftPos( +amt );
			}
			else if( ke->modifiers() & Qt::AltModifier) {
				Pattern * p = m_pattern->nextPattern();
				if(p != NULL)
				{
					setCurrentPattern(p);
				}
			}
			else
			{
				// scroll
				m_leftRightScroll->setValue(
						m_leftRightScroll->value() +
							cm_scrollAmtHoriz );

				// if they are moving notes around or resizing,
				// recalculate the note/resize position
				if( m_action == ActionMoveNote ||
						m_action == ActionResizeNote )
				{
					dragNotes( m_lastMouseX, m_lastMouseY,
								ke->modifiers() & Qt::AltModifier,
								ke->modifiers() & Qt::ShiftModifier );
				}

			}
			ke->accept();
			break;

		case Qt::Key_A:
			if( ke->modifiers() & Qt::ControlModifier )
			{
				ke->accept();
				selectAll();
				update();
			}
			break;

		case Qt::Key_Delete:
			deleteSelectedNotes();
			ke->accept();
			break;

		case Qt::Key_Home:
			m_timeLine->pos().setTicks( 0 );
			m_timeLine->updatePosition();
			ke->accept();
			break;

		case Qt::Key_0:
		case Qt::Key_1:
		case Qt::Key_2:
		case Qt::Key_3:
		case Qt::Key_4:
		case Qt::Key_5:
		case Qt::Key_6:
		case Qt::Key_7:
		case Qt::Key_8:
		case Qt::Key_9:
		{
			int len = 1 + ke->key() - Qt::Key_0;
			if( len == 10 )
			{
				len = 0;
			}
			if( ke->modifiers() & ( Qt::ControlModifier | Qt::KeypadModifier ) )
			{
				m_noteLenModel.setValue( len );
				ke->accept();
			}
			else if( ke->modifiers() & Qt::AltModifier )
			{
				m_quantizeModel.setValue( len );
				ke->accept();
			}
			break;
		}

		case Qt::Key_Control:
			if ( isActiveWindow() )
			{
				m_ctrlMode = m_editMode;
				m_editMode = ModeSelect;
				QApplication::changeOverrideCursor( Qt::ArrowCursor );
				ke->accept();
			}
			break;
		default:
			break;
	}

	update();
}




void PianoRoll::keyReleaseEvent(QKeyEvent* ke )
{
	if( hasValidPattern() && ke->modifiers() == Qt::NoModifier )
	{
		const int key_num = PianoView::getKeyFromKeyEvent( ke ) + ( DefaultOctave - 1 ) * KeysPerOctave;

		if( ! ke->isAutoRepeat() && key_num > -1 )
		{
			m_pattern->instrumentTrack()->pianoModel()->handleKeyRelease( key_num );
			ke->accept();
		}
	}

	switch( ke->key() )
	{
		case Qt::Key_Control:
			computeSelectedNotes( ke->modifiers() & Qt::ShiftModifier);
			m_editMode = m_ctrlMode;
			update();
			break;

		// update after undo/redo
		case Qt::Key_Z:
		case Qt::Key_R:
			if( hasValidPattern() && ke->modifiers() == Qt::ControlModifier )
			{
				update();
			}
			break;
	}

	update();
}




void PianoRoll::leaveEvent(QEvent * e )
{
	while( QApplication::overrideCursor() != NULL )
	{
		QApplication::restoreOverrideCursor();
	}

	QWidget::leaveEvent( e );
	s_textFloat->hide();
}




inline int PianoRoll::noteEditTop() const
{
	return height() - PR_BOTTOM_MARGIN -
		m_notesEditHeight + NOTE_EDIT_RESIZE_BAR;
}




inline int PianoRoll::noteEditBottom() const
{
	return height() - PR_BOTTOM_MARGIN;
}




inline int PianoRoll::noteEditRight() const
{
	return width() - PR_RIGHT_MARGIN;
}




inline int PianoRoll::noteEditLeft() const
{
	return WHITE_KEY_WIDTH;
}




inline int PianoRoll::keyAreaTop() const
{
	return PR_TOP_MARGIN;
}




inline int PianoRoll::keyAreaBottom() const
{
	return height() - PR_BOTTOM_MARGIN - m_notesEditHeight;
}




void PianoRoll::mousePressEvent(QMouseEvent * me )
{
	m_startedWithShift = me->modifiers() & Qt::ShiftModifier;

	if( ! hasValidPattern() )
	{
		return;
	}

	if( m_editMode == ModeEditDetuning && noteUnderMouse() )
	{
		static AutomationPattern* detuningPattern = nullptr;
		if (detuningPattern != nullptr)
		{
			detuningPattern->disconnect(this);
		}
		Note* n = noteUnderMouse();
		if (n->detuning() == nullptr)
		{
			n->createDetuning();
		}
		detuningPattern = n->detuning()->automationPattern();
		connect(detuningPattern, SIGNAL(dataChanged()), this, SLOT(update()));
		gui->automationEditor()->open(detuningPattern);
		return;
	}

	// if holding control, go to selection mode
	if( me->modifiers() & Qt::ControlModifier && m_editMode != ModeSelect )
	{
		m_ctrlMode = m_editMode;
		m_editMode = ModeSelect;
		QApplication::changeOverrideCursor( QCursor( Qt::ArrowCursor ) );
		update();
	}

	// keep track of the point where the user clicked down
	if( me->button() == Qt::LeftButton )
	{
		m_moveStartX = me->x();
		m_moveStartY = me->y();
	}

	if( me->y() > keyAreaBottom() && me->y() < noteEditTop() )
	{
		// resizing the note edit area
		m_action = ActionResizeNoteEditArea;
		m_oldNotesEditHeight = m_notesEditHeight;
		return;
	}

	if( me->y() > PR_TOP_MARGIN )
	{
		bool edit_note = ( me->y() > noteEditTop() );

		int key_num = getKey( me->y() );

		int x = me->x();


		if( x > WHITE_KEY_WIDTH )
		{
			// set, move or resize note

			x -= WHITE_KEY_WIDTH;

			// get tick in which the user clicked
			int pos_ticks = x * MidiTime::ticksPerTact() / m_ppt +
							m_currentPosition;


			// get note-vector of current pattern
			const NoteVector & notes = m_pattern->notes();

			// will be our iterator in the following loop
			NoteVector::ConstIterator it = notes.begin()+notes.size()-1;

			// loop through whole note-vector...
			for( int i = 0; i < notes.size(); ++i )
			{
				Note *note = *it;
				MidiTime len = note->length();
				if( len < 0 )
				{
					len = 4;
				}
				// and check whether the user clicked on an
				// existing note or an edit-line
				if( pos_ticks >= note->pos() &&
						len > 0 &&
					(
					( ! edit_note &&
					pos_ticks <= note->pos() + len &&
					note->key() == key_num )
					||
					( edit_note &&
					pos_ticks <= note->pos() +
						NOTE_EDIT_LINE_WIDTH * MidiTime::ticksPerTact() / m_ppt )
					)
					)
				{
					break;
				}
				--it;
			}

			// first check whether the user clicked in note-edit-
			// area
			if( edit_note )
			{
				m_pattern->addJournalCheckPoint();
				// scribble note edit changes
				mouseMoveEvent( me );
				return;
			}
			// left button??
			else if( me->button() == Qt::LeftButton &&
							m_editMode == ModeDraw )
			{
				// whether this action creates new note(s) or not
				bool is_new_note = false;

				Note * created_new_note = NULL;
				// did it reach end of vector because
				// there's no note??
				if( it == notes.begin()-1 )
				{
					is_new_note = true;
					m_pattern->addJournalCheckPoint();
					m_pattern->setType( Pattern::MelodyPattern );

					// then set new note

					// clear selection and select this new note
					clearSelectedNotes();

					// +32 to quanitize the note correctly when placing notes with
					// the mouse.  We do this here instead of in note.quantized
					// because live notes should still be quantized at the half.
					MidiTime note_pos( pos_ticks - ( quantization() / 2 ) );
					MidiTime note_len( newNoteLen() );

					Note new_note( note_len, note_pos, key_num );
					new_note.setSelected( true );
					new_note.setPanning( m_lastNotePanning );
					new_note.setVolume( m_lastNoteVolume );
					created_new_note = m_pattern->addNote( new_note );

					const InstrumentFunctionNoteStacking::Chord & chord = InstrumentFunctionNoteStacking::ChordTable::getInstance()
						.getChordByName( m_chordModel.currentText() );

					if( ! chord.isEmpty() )
					{
						// if a chord is selected, create following notes in chord
						// or arpeggio mode
						const bool arpeggio = me->modifiers() & Qt::ShiftModifier;
						for( int i = 1; i < chord.size(); i++ )
						{
							if( arpeggio )
							{
								note_pos += note_len;
							}
							Note new_note( note_len, note_pos, key_num + chord[i] );
							new_note.setSelected( true );
							new_note.setPanning( m_lastNotePanning );
							new_note.setVolume( m_lastNoteVolume );
							m_pattern->addNote( new_note );
						}
					}

					// reset it so that it can be used for
					// ops (move, resize) after this
					// code-block
					it = notes.begin();
					while( it != notes.end() && *it != created_new_note )
					{
						++it;
					}
				}

				Note *current_note = *it;
				m_currentNote = current_note;
				m_lastNotePanning = current_note->getPanning();
				m_lastNoteVolume = current_note->getVolume();
				m_lenOfNewNotes = current_note->length();

				// remember which key and tick we started with
				m_mouseDownKey = m_startKey;
				m_mouseDownTick = m_currentPosition;

				bool first = true;
				for( it = notes.begin(); it != notes.end(); ++it )
				{
					Note *note = *it;

					// remember note starting positions
					note->setOldKey( note->key() );
					note->setOldPos( note->pos() );
					note->setOldLength( note->length() );

					if( note->selected() )
					{

						// figure out the bounding box of all the selected notes
						if( first )
						{
							m_moveBoundaryLeft = note->pos().getTicks();
							m_moveBoundaryRight = note->endPos();
							m_moveBoundaryBottom = note->key();
							m_moveBoundaryTop = note->key();

							first = false;
						}
						else
						{
							m_moveBoundaryLeft = qMin(
												note->pos().getTicks(),
												(tick_t) m_moveBoundaryLeft );
							m_moveBoundaryRight = qMax( (int) note->endPos(),
													m_moveBoundaryRight );
							m_moveBoundaryBottom = qMin( note->key(),
											   m_moveBoundaryBottom );
							m_moveBoundaryTop = qMax( note->key(),
														m_moveBoundaryTop );
						}
					}
				}

				// if clicked on an unselected note, remove selection
				// and select that new note
				if( ! m_currentNote->selected() )
				{
					clearSelectedNotes();
					m_currentNote->setSelected( true );
					m_moveBoundaryLeft = m_currentNote->pos().getTicks();
					m_moveBoundaryRight = m_currentNote->endPos();
					m_moveBoundaryBottom = m_currentNote->key();
					m_moveBoundaryTop = m_currentNote->key();
				}


				// clicked at the "tail" of the note?
				if( pos_ticks * m_ppt / MidiTime::ticksPerTact() >
						m_currentNote->endPos() * m_ppt / MidiTime::ticksPerTact() - RESIZE_AREA_WIDTH
					&& m_currentNote->length() > 0 )
				{
					m_pattern->addJournalCheckPoint();
					// then resize the note
					m_action = ActionResizeNote;

					// set resize-cursor
					QCursor c( Qt::SizeHorCursor );
					QApplication::setOverrideCursor( c );
				}
				else
				{
					if( ! created_new_note )
					{
						m_pattern->addJournalCheckPoint();
					}

					// otherwise move it
					m_action = ActionMoveNote;

					// set move-cursor
					QCursor c( Qt::SizeAllCursor );
					QApplication::setOverrideCursor( c );

					// if they're holding shift, copy all selected notes
					if( ! is_new_note && me->modifiers() & Qt::ShiftModifier )
					{
						// vector to hold new notes until we're through the loop
						QVector<Note> newNotes;
						for( Note* const& note : notes )
						{
							if( note->selected() )
							{
								// copy this note
								Note noteCopy( *note );
								newNotes.push_back( noteCopy );
							}
							++it;
						}

						if( newNotes.size() != 0 )
						{
							//put notes from vector into piano roll
							for( int i = 0; i < newNotes.size(); ++i)
							{
								Note * newNote = m_pattern->addNote( newNotes[i] );
								newNote->setSelected( false );
							}

							// added new notes, so must update engine, song, etc
							Engine::getSong()->setModified();
							update();
							gui->songEditor()->update();
						}
					}

					// play the note
					testPlayNote( m_currentNote );
				}

				Engine::getSong()->setModified();
			}
			else if( ( me->buttons() == Qt::RightButton &&
							m_editMode == ModeDraw ) ||
					m_editMode == ModeErase )
			{
				// erase single note
				m_mouseDownRight = true;
				if( it != notes.begin()-1 )
				{
					Note *note = *it;
					m_pattern->addJournalCheckPoint();
					if( note->length() > 0 )
					{
						m_pattern->removeNote( note );
					}
					else
					{
						note->setLength( 0 );
						m_pattern->dataChanged();
					}
					Engine::getSong()->setModified();
				}
			}
			else if( me->button() == Qt::LeftButton &&
							m_editMode == ModeSelect )
			{
				// select an area of notes

				m_selectStartTick = pos_ticks;
				m_selectedTick = 0;
				m_selectStartKey = key_num;
				m_selectedKeys = 1;
				m_action = ActionSelectNotes;

				// call mousemove to fix glitch where selection
				// appears in wrong spot on mousedown
				mouseMoveEvent( me );
			}

			update();
		}
		else if( me->y() < keyAreaBottom() )
		{
			// clicked on keyboard on the left
			if( me->buttons() == Qt::RightButton )
			{
				// right click, tone marker contextual menu
				m_semiToneMarkerMenu->popup( mapToGlobal( QPoint( me->x(), me->y() ) ) );
			}
			else
			{
				// left click - play the note
				m_lastKey = key_num;
				int v = ( (float) x ) / ( (float) WHITE_KEY_WIDTH ) * MidiDefaultVelocity;
				m_pattern->instrumentTrack()->pianoModel()->handleKeyPress( key_num, v );
			}
		}
		else
		{
			if( me->buttons() == Qt::LeftButton )
			{
				// clicked in the box below the keys to the left of note edit area
				m_noteEditMode = (NoteEditMode)(((int)m_noteEditMode)+1);
				if( m_noteEditMode == NoteEditCount )
				{
					m_noteEditMode = (NoteEditMode) 0;
				}
				repaint();
			}
			else if( me->buttons() == Qt::RightButton )
			{
				// pop menu asking which one they want to edit
				m_noteEditMenu->popup( mapToGlobal( QPoint( me->x(), me->y() ) ) );
			}
		}
	}
}




void PianoRoll::mouseDoubleClickEvent(QMouseEvent * me )
{
	if( ! hasValidPattern() )
	{
		return;
	}

	// if they clicked in the note edit area, enter value for the volume bar
	if( me->x() > noteEditLeft() && me->x() < noteEditRight()
		&& me->y() > noteEditTop() && me->y() < noteEditBottom() )
	{
		// get values for going through notes
		int pixel_range = 4;
		int x = me->x() - WHITE_KEY_WIDTH;
		const int ticks_start = ( x-pixel_range/2 ) *
					MidiTime::ticksPerTact() / m_ppt + m_currentPosition;
		const int ticks_end = ( x+pixel_range/2 ) *
					MidiTime::ticksPerTact() / m_ppt + m_currentPosition;
		const int ticks_middle = x * MidiTime::ticksPerTact() / m_ppt + m_currentPosition;

		// get note-vector of current pattern
		NoteVector notes;
		notes += m_pattern->notes();

		// go through notes to figure out which one we want to change
		NoteVector nv;
		foreach( Note * i, notes )
		{
			if( i->pos().getTicks() >= ticks_start
				&& i->pos().getTicks() <= ticks_end
				&& i->length().getTicks() != 0
				&& ( i->selected() || ! isSelection() ) )
			{
				nv += i;
			}
		}
		// make sure we're on a note
		if( nv.size() > 0 )
		{
			Note * closest = NULL;
			int closest_dist = 9999999;
			// if we caught multiple notes, find the closest...
			if( nv.size() > 1 )
			{
				foreach( Note * i, nv )
				{
					const int dist = qAbs( i->pos().getTicks() - ticks_middle );
					if( dist < closest_dist ) { closest = i; closest_dist = dist; }
				}
				// ... then remove all notes from the vector that aren't on the same exact time
				NoteVector::Iterator it = nv.begin();
				while( it != nv.end() )
				{
					Note *note = *it;
					if( note->pos().getTicks() != closest->pos().getTicks() )
					{
						it = nv.erase( it );
					}
					else
					{
						it++;
					}
				}
			}
			enterValue( &nv );
		}
	}
}




void PianoRoll::testPlayNote( Note * n )
{
	m_lastKey = n->key();

	if( ! n->isPlaying() && ! m_recording )
	{
		n->setIsPlaying( true );

		const int baseVelocity = m_pattern->instrumentTrack()->midiPort()->baseVelocity();

		m_pattern->instrumentTrack()->pianoModel()->handleKeyPress( n->key(), n->midiVelocity( baseVelocity ) );

		MidiEvent event( MidiMetaEvent, -1, n->key(), panningToMidi( n->getPanning() ) );

		event.setMetaEvent( MidiNotePanning );

		m_pattern->instrumentTrack()->processInEvent( event, 0 );
	}
}




void PianoRoll::pauseTestNotes( bool pause )
{
	const NoteVector & notes = m_pattern->notes();
	NoteVector::ConstIterator it = notes.begin();
	while( it != notes.end() )
	{
		Note *note = *it;
		if( note->isPlaying() )
		{
			if( pause )
			{
				// stop note
				m_pattern->instrumentTrack()->pianoModel()->handleKeyRelease( note->key() );
			}
			else
			{
				// start note
				note->setIsPlaying( false );
				testPlayNote( note );
			}
		}

		++it;
	}
}




void PianoRoll::testPlayKey( int key, int velocity, int pan )
{
	// turn off old key
	m_pattern->instrumentTrack()->pianoModel()->handleKeyRelease( m_lastKey );

	// remember which one we're playing
	m_lastKey = key;

	// play new key
	m_pattern->instrumentTrack()->pianoModel()->handleKeyPress( key, velocity );
}




void PianoRoll::computeSelectedNotes(bool shift)
{
	if( m_selectStartTick == 0 &&
		m_selectedTick == 0 &&
		m_selectStartKey == 0 &&
		m_selectedKeys == 0 )
	{
		// don't bother, there's no selection
		return;
	}

	// setup selection-vars
	int sel_pos_start = m_selectStartTick;
	int sel_pos_end = m_selectStartTick+m_selectedTick;
	if( sel_pos_start > sel_pos_end )
	{
		qSwap<int>( sel_pos_start, sel_pos_end );
	}

	int sel_key_start = m_selectStartKey - m_startKey + 1;
	int sel_key_end = sel_key_start + m_selectedKeys;
	if( sel_key_start > sel_key_end )
	{
		qSwap<int>( sel_key_start, sel_key_end );
	}

	//int y_base = noteEditTop() - 1;
	if( hasValidPattern() )
	{
		const NoteVector & notes = m_pattern->notes();
		NoteVector::ConstIterator it;

		for( it = notes.begin(); it != notes.end(); ++it )
		{
			Note *note = *it;
			// make a new selection unless they're holding shift
			if( ! shift )
			{
				note->setSelected( false );
			}

			int len_ticks = note->length();

			if( len_ticks == 0 )
			{
				continue;
			}
			else if( len_ticks < 0 )
			{
				len_ticks = 4;
			}

			const int key = note->key() - m_startKey + 1;

			int pos_ticks = note->pos();

			// if the selection even barely overlaps the note
			if( key > sel_key_start &&
				key <= sel_key_end &&
				pos_ticks + len_ticks > sel_pos_start &&
				pos_ticks < sel_pos_end )
			{
				// remove from selection when holding shift
				bool selected = shift && note->selected();
				note->setSelected( ! selected);
			}
		}
	}

	removeSelection();
	update();
}




void PianoRoll::mouseReleaseEvent(QMouseEvent * me )
{
	s_textFloat->hide();
	bool mustRepaint = false;

	if( me->button() & Qt::LeftButton )
	{
		m_mouseDownLeft = false;
		mustRepaint = true;
	}
	if( me->button() & Qt::RightButton )
	{
		m_mouseDownRight = false;
		mustRepaint = true;
	}

	if( me->button() & Qt::LeftButton &&
					m_editMode == ModeSelect &&
					m_action == ActionSelectNotes )
	{
		// select the notes within the selection rectangle and
		// then destroy the selection rectangle

		computeSelectedNotes( me->modifiers() & Qt::ShiftModifier );

	}
	else if( me->button() & Qt::LeftButton &&
					m_action == ActionMoveNote )
	{
		// we moved one or more notes so they have to be
		// moved properly according to new starting-
		// time in the note-array of pattern

		m_pattern->rearrangeAllNotes();

	}
	if( me->button() & Qt::LeftButton &&
	   ( m_action == ActionMoveNote || m_action == ActionResizeNote ) )
	{
		// if we only moved one note, deselect it so we can
		// edit the notes in the note edit area
		if( selectionCount() == 1 )
		{
			clearSelectedNotes();
		}
	}


	if( hasValidPattern() )
	{
		// turn off all notes that are playing
		const NoteVector & notes = m_pattern->notes();

		NoteVector::ConstIterator it = notes.begin();
		while( it != notes.end() )
		{
			Note *note = *it;
			if( note->isPlaying() )
			{
				m_pattern->instrumentTrack()->pianoModel()->handleKeyRelease( note->key() );
				note->setIsPlaying( false );
			}

			++it;
		}

		// stop playing keys that we let go of
		m_pattern->instrumentTrack()->pianoModel()->handleKeyRelease( m_lastKey );
	}

	m_currentNote = NULL;

	m_action = ActionNone;

	if( m_editMode == ModeDraw )
	{
		QApplication::restoreOverrideCursor();
	}

	if( mustRepaint )
	{
		repaint();
	}
}




void PianoRoll::mouseMoveEvent( QMouseEvent * me )
{
	if( ! hasValidPattern() )
	{
		update();
		return;
	}

	if( m_action == ActionNone && me->buttons() == 0 )
	{
		if( me->y() > keyAreaBottom() && me->y() < noteEditTop() )
		{
			QApplication::setOverrideCursor(
					QCursor( Qt::SizeVerCursor ) );
			return;
		}
	}
	else if( m_action == ActionResizeNoteEditArea )
	{
		// change m_notesEditHeight and then repaint
		m_notesEditHeight = tLimit<int>(
					m_oldNotesEditHeight - ( me->y() - m_moveStartY ),
					NOTE_EDIT_MIN_HEIGHT,
					height() - PR_TOP_MARGIN - NOTE_EDIT_RESIZE_BAR -
									PR_BOTTOM_MARGIN - KEY_AREA_MIN_HEIGHT );
		repaint();
		return;
	}

	if( me->y() > PR_TOP_MARGIN || m_action != ActionNone )
	{
		bool edit_note = ( me->y() > noteEditTop() )
						&& m_action != ActionSelectNotes;


		int key_num = getKey( me->y() );
		int x = me->x();

		// see if they clicked on the keyboard on the left
		if( x < WHITE_KEY_WIDTH && m_action == ActionNone
		    && ! edit_note && key_num != m_lastKey
			&& me->buttons() & Qt::LeftButton )
		{
			// clicked on a key, play the note
			testPlayKey( key_num, ( (float) x ) / ( (float) WHITE_KEY_WIDTH ) * MidiDefaultVelocity, 0 );
			update();
			return;
		}

		x -= WHITE_KEY_WIDTH;

		if( me->buttons() & Qt::LeftButton
			&& m_editMode == ModeDraw
			&& (m_action == ActionMoveNote || m_action == ActionResizeNote ) )
		{
			// handle moving notes and resizing them
			bool replay_note = key_num != m_lastKey
							&& m_action == ActionMoveNote;

			if( replay_note || ( m_action == ActionMoveNote && ( me->modifiers() & Qt::ShiftModifier ) && ! m_startedWithShift ) )
			{
				pauseTestNotes();
			}

			dragNotes(
				me->x(),
				me->y(),
				me->modifiers() & Qt::AltModifier,
				me->modifiers() & Qt::ShiftModifier
			);

			if( replay_note && m_action == ActionMoveNote && ! ( ( me->modifiers() & Qt::ShiftModifier ) && ! m_startedWithShift ) )
			{
				pauseTestNotes( false );
			}
		}
		else if( ( edit_note || m_action == ActionChangeNoteProperty ) &&
				( me->buttons() & Qt::LeftButton || me->buttons() & Qt::MiddleButton
				|| ( me->buttons() & Qt::RightButton && me->modifiers() & Qt::ShiftModifier ) ) )
		{
			// editing note properties

			// Change notes within a certain pixel range of where
			// the mouse cursor is
			int pixel_range = 14;

			// convert to ticks so that we can check which notes
			// are in the range
			int ticks_start = ( x-pixel_range/2 ) *
					MidiTime::ticksPerTact() / m_ppt + m_currentPosition;
			int ticks_end = ( x+pixel_range/2 ) *
					MidiTime::ticksPerTact() / m_ppt + m_currentPosition;

			// get note-vector of current pattern
			const NoteVector & notes = m_pattern->notes();

			// determine what volume/panning to set note to
			// if middle-click, set to defaults
			volume_t vol;
			panning_t pan;

			if( me->buttons() & Qt::LeftButton )
			{
				vol = tLimit<int>( MinVolume +
								( ( (float)noteEditBottom() ) - ( (float)me->y() ) ) /
								( (float)( noteEditBottom() - noteEditTop() ) ) *
								( MaxVolume - MinVolume ),
											MinVolume, MaxVolume );
				pan = tLimit<int>( PanningLeft +
								( (float)( noteEditBottom() - me->y() ) ) /
								( (float)( noteEditBottom() - noteEditTop() ) ) *
								( (float)( PanningRight - PanningLeft ) ),
										  PanningLeft, PanningRight);
			}
			else
			{
				vol = DefaultVolume;
				pan = DefaultPanning;
			}

			if( m_noteEditMode == NoteEditVolume )
			{
				m_lastNoteVolume = vol;
				//! \todo display velocity for MIDI-based instruments
				// possibly dBV values too? not sure if it makes sense for note volumes...
				s_textFloat->setText( tr("Volume: %1%").arg( vol ) );
			}
			else if( m_noteEditMode == NoteEditPanning )
			{
				m_lastNotePanning = pan;
				if( pan < 0 )
				{
					s_textFloat->setText( tr("Panning: %1% left").arg( qAbs( pan ) ) );
				}
				else if( pan > 0 )
				{
					s_textFloat->setText( tr("Panning: %1% right").arg( qAbs( pan ) ) );
				}
				else
				{
					s_textFloat->setText( tr("Panning: center") );
				}
			}



			// loop through vector
			bool on_note = false;
			bool use_selection = isSelection();
			NoteVector::ConstIterator it = notes.begin()+notes.size()-1;
			for( int i = 0; i < notes.size(); ++i )
			{
				Note * n = *it;
				if( n->pos().getTicks() >= ticks_start
					&& n->pos().getTicks() <= ticks_end
					&& n->length().getTicks() != 0
					&& ( n->selected() || ! use_selection ) )
				{
					on_note = true;
					m_pattern->dataChanged();

					// play the note so that the user can tell how loud it is
					// and where it is panned
					testPlayNote( n );

					if( m_noteEditMode == NoteEditVolume )
					{
						n->setVolume( vol );

						const int baseVelocity = m_pattern->instrumentTrack()->midiPort()->baseVelocity();

						m_pattern->instrumentTrack()->processInEvent( MidiEvent( MidiKeyPressure, -1, n->key(), n->midiVelocity( baseVelocity ) ) );
					}
					else if( m_noteEditMode == NoteEditPanning )
					{
						n->setPanning( pan );
						MidiEvent evt( MidiMetaEvent, -1, n->key(), panningToMidi( pan ) );
						evt.setMetaEvent( MidiNotePanning );
						m_pattern->instrumentTrack()->processInEvent( evt );
					}
				}
				else
				{
					if( n->isPlaying() )
					{
						// mouse not over this note, stop playing it.
						m_pattern->instrumentTrack()->pianoModel()->handleKeyRelease( n->key() );

						n->setIsPlaying( false );
					}
				}

				// set textfloat visible if we're on a note
				if( on_note )
				{
					s_textFloat->moveGlobal( this,	QPoint( me->x() + 4, me->y() + 16 ) );
					s_textFloat->show();
				}
				else
				{
					s_textFloat->hide();
				}

				--it;

			}
		}

		else if( me->buttons() == Qt::NoButton && m_editMode == ModeDraw )
		{
			// set move- or resize-cursor

			// get tick in which the cursor is posated
			int pos_ticks = ( x * MidiTime::ticksPerTact() ) /
						m_ppt + m_currentPosition;

			// get note-vector of current pattern
			const NoteVector & notes = m_pattern->notes();

			// will be our iterator in the following loop
			NoteVector::ConstIterator it = notes.begin()+notes.size()-1;

			// loop through whole note-vector...
			for( int i = 0; i < notes.size(); ++i )
			{
				Note *note = *it;
				// and check whether the cursor is over an
				// existing note
				if( pos_ticks >= note->pos() &&
			    		pos_ticks <= note->pos() +
							note->length() &&
					note->key() == key_num &&
					note->length() > 0 )
				{
					break;
				}
				--it;
			}

			// did it reach end of vector because there's
			// no note??
			if( it != notes.begin()-1 )
			{
				Note *note = *it;
				// cursor at the "tail" of the note?
				if( note->length() > 0 &&
					pos_ticks*m_ppt /
						MidiTime::ticksPerTact() >
						( note->pos() +
						note->length() )*m_ppt/
						MidiTime::ticksPerTact()-
							RESIZE_AREA_WIDTH )
				{
					if( QApplication::overrideCursor() )
					{
	if( QApplication::overrideCursor()->shape() != Qt::SizeHorCursor )
						{
				while( QApplication::overrideCursor() != NULL )
				{
					QApplication::restoreOverrideCursor();
				}

				QCursor c( Qt::SizeHorCursor );
				QApplication::setOverrideCursor( c );
						}
					}
					else
					{
						QCursor c( Qt::SizeHorCursor );
						QApplication::setOverrideCursor(
									c );
					}
				}
				else
				{
					if( QApplication::overrideCursor() )
					{
	if( QApplication::overrideCursor()->shape() != Qt::SizeAllCursor )
						{
				while( QApplication::overrideCursor() != NULL )
				{
					QApplication::restoreOverrideCursor();
				}

						QCursor c( Qt::SizeAllCursor );
						QApplication::setOverrideCursor(
									c );
						}
					}
					else
					{
						QCursor c( Qt::SizeAllCursor );
						QApplication::setOverrideCursor(
									c );
					}
				}
			}
			else
			{
				// the cursor is over no note, so restore cursor
				while( QApplication::overrideCursor() != NULL )
				{
					QApplication::restoreOverrideCursor();
				}
			}
		}
		else if( me->buttons() & Qt::LeftButton &&
						m_editMode == ModeSelect &&
						m_action == ActionSelectNotes )
		{

			// change size of selection

			// get tick in which the cursor is posated
			int pos_ticks = x * MidiTime::ticksPerTact() / m_ppt +
							m_currentPosition;

			m_selectedTick = pos_ticks - m_selectStartTick;
			if( (int) m_selectStartTick + m_selectedTick < 0 )
			{
				m_selectedTick = -static_cast<int>(
							m_selectStartTick );
			}
			m_selectedKeys = key_num - m_selectStartKey;
			if( key_num <= m_selectStartKey )
			{
				--m_selectedKeys;
			}
		}
		else if( m_editMode == ModeDraw && me->buttons() & Qt::RightButton )
		{
			// holding down right-click to delete notes

			// get tick in which the user clicked
			int pos_ticks = x * MidiTime::ticksPerTact() / m_ppt +
							m_currentPosition;


			// get note-vector of current pattern
			const NoteVector & notes = m_pattern->notes();

			// will be our iterator in the following loop
			NoteVector::ConstIterator it = notes.begin();

			// loop through whole note-vector...
			while( it != notes.end() )
			{
				Note *note = *it;
				MidiTime len = note->length();
				if( len < 0 )
				{
					len = 4;
				}
				// and check whether the user clicked on an
				// existing note or an edit-line
				if( pos_ticks >= note->pos() &&
						len > 0 &&
					(
					( ! edit_note &&
					pos_ticks <= note->pos() + len &&
					note->key() == key_num )
					||
					( edit_note &&
					pos_ticks <= note->pos() +
							NOTE_EDIT_LINE_WIDTH *
						MidiTime::ticksPerTact() /
								m_ppt )
					)
					)
				{
					// delete this note
					if( it != notes.end() )
					{
						if( note->length() > 0 )
						{
							m_pattern->removeNote( note );
						}
						else
						{
							note->setLength( 0 );
							m_pattern->dataChanged();
						}
						Engine::getSong()->setModified();
					}
				}
				else
				{
					++it;
				}
			}
		}
	}
	else
	{
		if( me->buttons() & Qt::LeftButton &&
					m_editMode == ModeSelect &&
					m_action == ActionSelectNotes )
		{

			int x = me->x() - WHITE_KEY_WIDTH;
			if( x < 0 && m_currentPosition > 0 )
			{
				x = 0;
				QCursor::setPos( mapToGlobal( QPoint(
							WHITE_KEY_WIDTH,
							me->y() ) ) );
				if( m_currentPosition >= 4 )
				{
					m_leftRightScroll->setValue(
							m_currentPosition - 4 );
				}
				else
				{
					m_leftRightScroll->setValue( 0 );
				}
			}
			else if( x > width() - WHITE_KEY_WIDTH )
			{
				x = width() - WHITE_KEY_WIDTH;
				QCursor::setPos( mapToGlobal( QPoint( width(),
							me->y() ) ) );
				m_leftRightScroll->setValue( m_currentPosition +
									4 );
			}

			// get tick in which the cursor is posated
			int pos_ticks = x * MidiTime::ticksPerTact()/ m_ppt +
							m_currentPosition;

			m_selectedTick = pos_ticks -
							m_selectStartTick;
			if( (int) m_selectStartTick + m_selectedTick <
									0 )
			{
				m_selectedTick = -static_cast<int>(
							m_selectStartTick );
			}


			int key_num = getKey( me->y() );
			int visible_keys = ( height() - PR_TOP_MARGIN -
						PR_BOTTOM_MARGIN -
						m_notesEditHeight ) /
							KEY_LINE_HEIGHT + 2;
			const int s_key = m_startKey - 1;

			if( key_num <= s_key )
			{
				QCursor::setPos( mapToGlobal( QPoint( me->x(),
							keyAreaBottom() ) ) );
				m_topBottomScroll->setValue(
					m_topBottomScroll->value() + 1 );
				key_num = s_key;
			}
			else if( key_num >= s_key + visible_keys )
			{
				QCursor::setPos( mapToGlobal( QPoint( me->x(),
							PR_TOP_MARGIN ) ) );
				m_topBottomScroll->setValue(
					m_topBottomScroll->value() - 1 );
				key_num = s_key + visible_keys;
			}

			m_selectedKeys = key_num - m_selectStartKey;
			if( key_num <= m_selectStartKey )
			{
				--m_selectedKeys;
			}
		}
		QApplication::restoreOverrideCursor();
	}

	m_lastMouseX = me->x();
	m_lastMouseY = me->y();

	update();
}




void PianoRoll::dragNotes( int x, int y, bool alt, bool shift )
{
	// dragging one or more notes around

	// convert pixels to ticks and keys
	int off_x = x - m_moveStartX;
	int off_ticks = off_x * MidiTime::ticksPerTact() / m_ppt;
	int off_key = getKey( y ) - getKey( m_moveStartY );

	// handle scroll changes while dragging
	off_ticks -= m_mouseDownTick - m_currentPosition;
	off_key -= m_mouseDownKey - m_startKey;


	// if they're not holding alt, quantize the offset
	if( ! alt )
	{
		off_ticks = floor( off_ticks / quantization() )
						* quantization();
	}

	// make sure notes won't go outside boundary conditions
	if( m_action == ActionMoveNote && ! ( shift && ! m_startedWithShift ) )
	{
		if( m_moveBoundaryLeft + off_ticks < 0 )
		{
			off_ticks += 0 - (off_ticks + m_moveBoundaryLeft);
		}
		if( m_moveBoundaryTop + off_key > NumKeys )
		{
			off_key -= NumKeys - (m_moveBoundaryTop + off_key);
		}
		if( m_moveBoundaryBottom + off_key < 0 )
		{
			off_key += 0 - (m_moveBoundaryBottom + off_key);
		}
	}

	int shift_offset = 0;
	int shift_ref_pos = -1;

	// get note-vector of current pattern
	const NoteVector & notes = m_pattern->notes();

	// will be our iterator in the following loop
	NoteVector::ConstIterator it = notes.begin();
	while( it != notes.end() )
	{
		Note *note = *it;
		const int pos = note->pos().getTicks();
		// when resizing a note and holding shift: shift the following
		// notes to preserve the melody
		if( m_action == ActionResizeNote && shift )
		{
			int shifted_pos = note->oldPos().getTicks() + shift_offset;
			if( shifted_pos && pos == shift_ref_pos )
			{
				shifted_pos -= off_ticks;
			}
			note->setPos( MidiTime( shifted_pos ) );
		}

		if( note->selected() )
		{
			if( m_action == ActionMoveNote && ! ( shift && ! m_startedWithShift ) )
			{
				// moving note
				int pos_ticks = note->oldPos().getTicks() + off_ticks;
				int key_num = note->oldKey() + off_key;

				// ticks can't be negative
				pos_ticks = qMax(0, pos_ticks);
				// upper/lower bound checks on key_num
				key_num = qMax(0, key_num);
				key_num = qMin(key_num, NumKeys);

				note->setPos( MidiTime( pos_ticks ) );
				note->setKey( key_num );
			}
			else if( m_action == ActionResizeNote )
			{
				// resizing note
				int ticks_new = note->oldLength().getTicks() + off_ticks;
				if( ticks_new <= 0 )
				{
					ticks_new = 1;
				}
				else if( shift )
				{
					// when holding shift: update the offset used to shift
					// the following notes
					if( pos > shift_ref_pos )
					{
						shift_offset += off_ticks;
						shift_ref_pos = pos;
					}
				}
				note->setLength( MidiTime( ticks_new ) );

				m_lenOfNewNotes = note->length();
			}
			else if( m_action == ActionMoveNote && ( shift && ! m_startedWithShift ) )
			{
				// quick resize, toggled by holding shift after starting a note move, but not before
				int ticks_new = note->oldLength().getTicks() + off_ticks;
				if( ticks_new <= 0 )
				{
					ticks_new = 1;
				}
				note->setLength( MidiTime( ticks_new ) );
				m_lenOfNewNotes = note->length();
			}
		}
		++it;
	}

	m_pattern->dataChanged();
	Engine::getSong()->setModified();
}

static QString calculateNoteLabel(QString note, int octave)
{
	if(note.isEmpty())
	{
		return "";
	}
	return note + QString::number(octave);
}

static void printNoteHeights(QPainter& p, int bottom, int width, int startKey)
{
	assert(Key_C == 0);
	assert(Key_H == 11);

	struct KeyLabel
	{
		QString key, minor, major;
	};
	const KeyLabel labels[12] = {
		{QObject::tr("C", "Note name")},
		{"", QObject::tr("Db", "Note name"), QObject::tr("C#", "Note name")},
		{QObject::tr("D", "Note name")},
		{"", QObject::tr("Eb", "Note name"), QObject::tr("D#", "Note name")},
		{QObject::tr("E", "Note name"), QObject::tr("Fb", "Note name")},
		{"F"},
		{"", QObject::tr("Gb", "Note name"), QObject::tr("F#", "Note name")},
		{QObject::tr("G", "Note name")},
		{"", QObject::tr("Ab", "Note name"),QObject::tr( "G#", "Note name")},
		{QObject::tr("A", "Note name")},
		{"", QObject::tr("Bb", "Note name"),QObject::tr( "A#", "Note name")},
		{QObject::tr("B", "Note name")}
	};

	p.setFont( pointSize<KEY_LINE_HEIGHT-4>( p.font() ) );
	p.setPen( QColor( 255, 255, 255 ) );
	for( int y = bottom, key = startKey; y > PR_TOP_MARGIN;
			y -= KEY_LINE_HEIGHT, key++)
	{
		const unsigned note = key % KeysPerOctave;
		assert( note < ( sizeof( labels ) / sizeof( labels[0] ) ));
		const KeyLabel& noteLabel( labels[note] );
		const int octave = key / KeysPerOctave;
		const KeyLabel notes = {
			calculateNoteLabel(noteLabel.key, octave),
			calculateNoteLabel(noteLabel.minor, octave),
			calculateNoteLabel(noteLabel.major, octave),
		};

		const int drawWidth( width - WHITE_KEY_WIDTH );
		const int hspace = 300;
		const int columnCount = drawWidth/hspace + 1;
		for(int col = 0; col < columnCount; col++)
		{
			const int subOffset = 42;
			const int x = subOffset + hspace/2 + hspace * col;
			p.drawText( WHITE_KEY_WIDTH + x, y, notes.key);
			p.drawText( WHITE_KEY_WIDTH + x - subOffset, y, notes.minor);
			p.drawText( WHITE_KEY_WIDTH + x + subOffset, y, notes.major);
		}
	}
}

void PianoRoll::paintEvent(QPaintEvent * pe )
{
	QColor horizCol = QColor( gridColor() );
	QColor vertCol = QColor( gridColor() );

	QStyleOption opt;
	opt.initFrom( this );
	QPainter p( this );
	style()->drawPrimitive( QStyle::PE_Widget, &opt, &p, this );

	QBrush bgColor = p.background();

	// fill with bg color
	p.fillRect( 0, 0, width(), height(), bgColor );

	// set font-size to 8
	p.setFont( pointSize<8>( p.font() ) );

	// y_offset is used to align the piano-keys on the key-lines
	int y_offset = 0;

	// calculate y_offset according to first key
	switch( prKeyOrder[m_startKey % KeysPerOctave] )
	{
		case PR_BLACK_KEY: y_offset = KEY_LINE_HEIGHT / 4; break;
		case PR_WHITE_KEY_BIG: y_offset = KEY_LINE_HEIGHT / 2; break;
		case PR_WHITE_KEY_SMALL:
			if( prKeyOrder[( ( m_startKey + 1 ) %
					KeysPerOctave)] != PR_BLACK_KEY )
			{
				y_offset = KEY_LINE_HEIGHT / 2;
			}
			break;
	}
	// start drawing at the bottom
	int key_line_y = keyAreaBottom() - 1;
	// used for aligning black-keys later
	int first_white_key_height = WHITE_KEY_SMALL_HEIGHT;
	// key-counter - only needed for finding out whether the processed
	// key is the first one
	int keys_processed = 0;

	int key = m_startKey;

	// display note marks before drawing other lines
	for( int i = 0; i < m_markedSemiTones.size(); i++ )
	{
		const int key_num = m_markedSemiTones.at( i );
		const int y = keyAreaBottom() + 5
			- KEY_LINE_HEIGHT * ( key_num - m_startKey + 1 );

		if( y > keyAreaBottom() )
		{
			break;
		}

		p.fillRect( WHITE_KEY_WIDTH + 1, y - KEY_LINE_HEIGHT / 2,
			    width() - 10, KEY_LINE_HEIGHT,
							QColor( 0, 80 - ( key_num % KeysPerOctave ) * 3, 64 + key_num / 2) );
	}


	// draw all white keys...
	for( int y = key_line_y + 1 + y_offset; y > PR_TOP_MARGIN;
			key_line_y -= KEY_LINE_HEIGHT, ++keys_processed )
	{
		// check for white key that is only half visible on the
		// bottom of piano-roll
		if( keys_processed == 0 &&
			prKeyOrder[m_startKey % KeysPerOctave] ==
								PR_BLACK_KEY )
		{
			// draw it!
			p.drawPixmap( PIANO_X, y - WHITE_KEY_SMALL_HEIGHT,
							*s_whiteKeySmallPm );
			// update y-pos
			y -= WHITE_KEY_SMALL_HEIGHT / 2;
			// move first black key down (we didn't draw whole
			// white key so black key needs to be lifted down)
			// (default for first_white_key_height =
			// WHITE_KEY_SMALL_HEIGHT, so WHITE_KEY_SMALL_HEIGHT/2
			// is smaller)
			first_white_key_height = WHITE_KEY_SMALL_HEIGHT / 2;
		}
		// check whether to draw a big or a small white key
		if( prKeyOrder[key % KeysPerOctave] == PR_WHITE_KEY_SMALL )
		{
			// draw a small one while checking if it is pressed or not
			if( hasValidPattern() && m_pattern->instrumentTrack()->pianoModel()->isKeyPressed( key ) )
			{
				p.drawPixmap( PIANO_X, y - WHITE_KEY_SMALL_HEIGHT, *s_whiteKeySmallPressedPm );
			}
			else
			{
				p.drawPixmap( PIANO_X, y - WHITE_KEY_SMALL_HEIGHT, *s_whiteKeySmallPm );
			}
			// update y-pos
			y -= WHITE_KEY_SMALL_HEIGHT;

		}
		else if( prKeyOrder[key % KeysPerOctave] == PR_WHITE_KEY_BIG )
		{
			// draw a big one while checking if it is pressed or not
			if( hasValidPattern() && m_pattern->instrumentTrack()->pianoModel()->isKeyPressed( key ) )
			{
				p.drawPixmap( PIANO_X, y - WHITE_KEY_BIG_HEIGHT, *s_whiteKeyBigPressedPm );
			}
			else
			{
				p.drawPixmap( PIANO_X, y-WHITE_KEY_BIG_HEIGHT, *s_whiteKeyBigPm );
			}
			// if a big white key has been the first key,
			// black keys needs to be lifted up
			if( keys_processed == 0 )
			{
				first_white_key_height = WHITE_KEY_BIG_HEIGHT;
			}
			// update y-pos
			y -= WHITE_KEY_BIG_HEIGHT;
		}
		// label C-keys...
		if( static_cast<Keys>( key % KeysPerOctave ) == Key_C )
		{
			const QString cLabel = "C" + QString::number( static_cast<int>( key / KeysPerOctave ) );
			p.setPen( QColor( 240, 240, 240 ) );
			p.drawText( C_KEY_LABEL_X + 1, y + 14, cLabel );
			p.setPen( QColor( 0, 0, 0 ) );
			p.drawText( C_KEY_LABEL_X, y + 13, cLabel );
			horizCol.setAlpha( 192 );
		}
		else
		{
			horizCol.setAlpha( 128 );
		}
		// draw key-line
		p.setPen( horizCol );
		p.drawLine( WHITE_KEY_WIDTH, key_line_y, width(), key_line_y );
		++key;
	}

	// reset all values, because now we're going to draw all black keys
	key = m_startKey;
	keys_processed = 0;
	int white_cnt = 0;

	// and go!
	for( int y = keyAreaBottom() + y_offset;
					y > PR_TOP_MARGIN; ++keys_processed )
	{
		// check for black key that is only half visible on the bottom
		// of piano-roll
		if( keys_processed == 0
		    // current key may not be a black one
		    && prKeyOrder[key % KeysPerOctave] != PR_BLACK_KEY
		    // but the previous one must be black (we must check this
		    // because there might be two white keys (E-F)
		    && prKeyOrder[( key - 1 ) % KeysPerOctave] ==
								PR_BLACK_KEY )
		{
			// draw the black key!
			p.drawPixmap( PIANO_X, y - BLACK_KEY_HEIGHT / 2,
								*s_blackKeyPm );
			// is the one after the start-note a black key??
			if( prKeyOrder[( key + 1 ) % KeysPerOctave] !=
								PR_BLACK_KEY )
			{
				// no, then move it up!
				y -= KEY_LINE_HEIGHT / 2;
			}
		}
		// current key black?
		if( prKeyOrder[key % KeysPerOctave] == PR_BLACK_KEY)
		{
			// then draw it (calculation of y very complicated,
			// but that's the only working solution, sorry...)
			// check if the key is pressed or not
			if( hasValidPattern() && m_pattern->instrumentTrack()->pianoModel()->isKeyPressed( key ) )
			{
				p.drawPixmap( PIANO_X, y - ( first_white_key_height -
						WHITE_KEY_SMALL_HEIGHT ) -
						WHITE_KEY_SMALL_HEIGHT/2 - 1 -
						BLACK_KEY_HEIGHT, *s_blackKeyPressedPm );
			}
		    else
			{
				p.drawPixmap( PIANO_X, y - ( first_white_key_height -
						WHITE_KEY_SMALL_HEIGHT ) -
						WHITE_KEY_SMALL_HEIGHT/2 - 1 -
						BLACK_KEY_HEIGHT, *s_blackKeyPm );
			}
			// update y-pos
			y -= WHITE_KEY_BIG_HEIGHT;
			// reset white-counter
			white_cnt = 0;
		}
		else
		{
			// simple workaround for increasing x if there were
			// two white keys (e.g. between E and F)
			++white_cnt;
			if( white_cnt > 1 )
			{
				y -= WHITE_KEY_BIG_HEIGHT/2;
			}
		}

		++key;
	}


	// erase the area below the piano, because there might be keys that
	// should be only half-visible
	p.fillRect( QRect( 0, keyAreaBottom(),
			WHITE_KEY_WIDTH, noteEditBottom() - keyAreaBottom() ), bgColor );

	// display note editing info
	QFont f = p.font();
	f.setBold( false );
	p.setFont( pointSize<10>( f ) );
	p.setPen( noteModeColor() );
	p.drawText( QRect( 0, keyAreaBottom(),
					  WHITE_KEY_WIDTH, noteEditBottom() - keyAreaBottom() ),
			   Qt::AlignCenter | Qt::TextWordWrap,
			   m_nemStr.at( m_noteEditMode ) + ":" );

	// set clipping area, because we are not allowed to paint over
	// keyboard...
	p.setClipRect( WHITE_KEY_WIDTH, PR_TOP_MARGIN,
				width() - WHITE_KEY_WIDTH,
				height() - PR_TOP_MARGIN - PR_BOTTOM_MARGIN );

	// draw vertical raster

	// triplet mode occurs if the note duration isn't a multiple of 3
	bool triplets = ( quantization() % 3 != 0 );

	int spt = MidiTime::stepsPerTact();
	float pp16th = (float)m_ppt / spt;
	int bpt = DefaultBeatsPerTact;
	if ( triplets ) {
		spt = static_cast<int>(1.5 * spt);
		bpt = static_cast<int>(bpt * 2.0/3.0);
		pp16th *= 2.0/3.0;
	}

	int tact_16th = m_currentPosition / bpt;

	const int offset = ( m_currentPosition % bpt ) *
			m_ppt / MidiTime::ticksPerTact();

	bool show32nds = ( m_zoomingModel.value() > 3 );

	// we need float here as odd time signatures might produce rounding
	// errors else and thus an unusable grid
	for( float x = WHITE_KEY_WIDTH - offset; x < width();
						x += pp16th, ++tact_16th )
	{
		if( x >= WHITE_KEY_WIDTH )
		{
			// every tact-start needs to be a bright line
			if( tact_16th % spt == 0 )
			{
	 			p.setPen( gridColor() );
			}
			// normal line
			else if( tact_16th % 4 == 0 )
			{
				vertCol.setAlpha( 160 );
				p.setPen( vertCol );
			}
			// weak line
			else
			{
				vertCol.setAlpha( 128 );
				p.setPen( vertCol );
			}

			p.drawLine( (int) x, PR_TOP_MARGIN, (int) x, height() -
							PR_BOTTOM_MARGIN );

			// extra 32nd's line
			if( show32nds )
			{
				vertCol.setAlpha( 80 );
				p.setPen( vertCol );
				p.drawLine( (int)(x + pp16th / 2) , PR_TOP_MARGIN,
						(int)(x + pp16th / 2), height() -
						PR_BOTTOM_MARGIN );
			}
		}
	}



	// following code draws all notes in visible area
	// and the note editing stuff (volume, panning, etc)

	// setup selection-vars
	int sel_pos_start = m_selectStartTick;
	int sel_pos_end = m_selectStartTick+m_selectedTick;
	if( sel_pos_start > sel_pos_end )
	{
		qSwap<int>( sel_pos_start, sel_pos_end );
	}

	int sel_key_start = m_selectStartKey - m_startKey + 1;
	int sel_key_end = sel_key_start + m_selectedKeys;
	if( sel_key_start > sel_key_end )
	{
		qSwap<int>( sel_key_start, sel_key_end );
	}

	int y_base = keyAreaBottom() - 1;
	if( hasValidPattern() )
	{
		p.setClipRect( WHITE_KEY_WIDTH, PR_TOP_MARGIN,
				width() - WHITE_KEY_WIDTH,
				height() - PR_TOP_MARGIN );

		const NoteVector & notes = m_pattern->notes();

		const int visible_keys = ( keyAreaBottom()-keyAreaTop() ) /
							KEY_LINE_HEIGHT + 2;

		QPolygon editHandles;
		NoteVector::ConstIterator it;

		for( it = notes.begin(); it != notes.end(); ++it )
		{
			Note *note = *it;
			int len_ticks = note->length();

			if( len_ticks == 0 )
			{
				continue;
			}
			else if( len_ticks < 0 )
			{
				len_ticks = 4;
			}

			const int key = note->key() - m_startKey + 1;

			int pos_ticks = note->pos();

			int note_width = len_ticks * m_ppt / MidiTime::ticksPerTact();
			const int x = ( pos_ticks - m_currentPosition ) *
					m_ppt / MidiTime::ticksPerTact();
			// skip this note if not in visible area at all
			if( !( x + note_width >= 0 && x <= width() - WHITE_KEY_WIDTH ) )
			{
				continue;
			}

			// is the note in visible area?
			if( key > 0 && key <= visible_keys )
			{

				// we've done and checked all, let's draw the
				// note
				drawNoteRect( p, x + WHITE_KEY_WIDTH,
						y_base - key * KEY_LINE_HEIGHT,
								note_width, note, noteColor() );
			}

			// draw note editing stuff
			int editHandleTop = 0;
			if( m_noteEditMode == NoteEditVolume )
			{
				QColor color = barColor().lighter( 30 + ( note->getVolume() * 90 / MaxVolume ) );
				if( note->selected() )
				{
					color.setRgb( 0x00, 0x40, 0xC0 );
				}
				p.setPen( QPen( color, NOTE_EDIT_LINE_WIDTH ) );

				editHandleTop = noteEditBottom() -
					( (float)( note->getVolume() - MinVolume ) ) /
					( (float)( MaxVolume - MinVolume ) ) *
					( (float)( noteEditBottom() - noteEditTop() ) );

				p.drawLine( noteEditLeft() + x, editHandleTop,
							noteEditLeft() + x, noteEditBottom() );

			}
			else if( m_noteEditMode == NoteEditPanning )
			{
				QColor color( noteColor() );
				if( note->selected() )
				{
					color.setRgb( 0x00, 0x40, 0xC0 );
				}

				p.setPen( QPen( color, NOTE_EDIT_LINE_WIDTH ) );

				editHandleTop = noteEditBottom() -
					( (float)( note->getPanning() - PanningLeft ) ) /
					( (float)( (PanningRight - PanningLeft ) ) ) *
					( (float)( noteEditBottom() - noteEditTop() ) );

				p.drawLine( noteEditLeft() + x, noteEditTop() +
						( (float)( noteEditBottom() - noteEditTop() ) ) / 2.0f,
						    noteEditLeft() + x, editHandleTop );
			}
			editHandles << QPoint( x + noteEditLeft(),
						editHandleTop+1 );

			if( note->hasDetuningInfo() )
			{
				drawDetuningInfo( p, *it,
					x + WHITE_KEY_WIDTH,
					y_base - key * KEY_LINE_HEIGHT );
			}
		}

		p.setPen( QPen( noteColor(), NOTE_EDIT_LINE_WIDTH + 2 ) );
		p.drawPoints( editHandles );

	}
	else
	{
		QFont f = p.font();
		f.setBold( true );
		p.setFont( pointSize<14>( f ) );
		p.setPen( QApplication::palette().color( QPalette::Active,
							QPalette::BrightText ) );
		p.drawText( WHITE_KEY_WIDTH + 20, PR_TOP_MARGIN + 40,
				tr( "Please open a pattern by double-clicking "
								"on it!" ) );
	}

	p.setClipRect( WHITE_KEY_WIDTH, PR_TOP_MARGIN, width() -
				WHITE_KEY_WIDTH, height() - PR_TOP_MARGIN -
					m_notesEditHeight - PR_BOTTOM_MARGIN );

	// now draw selection-frame
	int x = ( ( sel_pos_start - m_currentPosition ) * m_ppt ) /
						MidiTime::ticksPerTact();
	int w = ( ( ( sel_pos_end - m_currentPosition ) * m_ppt ) /
						MidiTime::ticksPerTact() ) - x;
	int y = (int) y_base - sel_key_start * KEY_LINE_HEIGHT;
	int h = (int) y_base - sel_key_end * KEY_LINE_HEIGHT - y;
	p.setPen( QColor( 0, 64, 192 ) );
	p.setBrush( Qt::NoBrush );
	p.drawRect( x + WHITE_KEY_WIDTH, y, w, h );

	// TODO: Get this out of paint event
	int l = ( hasValidPattern() )? (int) m_pattern->length() : 0;

	// reset scroll-range
	if( m_leftRightScroll->maximum() != l )
	{
		m_leftRightScroll->setRange( 0, l );
		m_leftRightScroll->setPageStep( l );
	}

	// set alpha for horizontal lines
	horizCol.setAlpha( 64 );

	// horizontal line for the key under the cursor
	if( hasValidPattern() )
	{
		int key_num = getKey( mapFromGlobal( QCursor::pos() ).y() );
		p.fillRect( 10, keyAreaBottom() + 3 - KEY_LINE_HEIGHT *
					( key_num - m_startKey + 1 ), width() - 10, KEY_LINE_HEIGHT - 7, horizCol );
	}

	// bar to resize note edit area
	p.setClipRect( 0, 0, width(), height() );
	p.fillRect( QRect( 0, keyAreaBottom(),
					width()-PR_RIGHT_MARGIN, NOTE_EDIT_RESIZE_BAR ), horizCol );

	const QPixmap * cursor = NULL;
	// draw current edit-mode-icon below the cursor
	switch( m_editMode )
	{
		case ModeDraw:
			if( m_mouseDownRight )
			{
				cursor = s_toolErase;
			}
			else if( m_action == ActionMoveNote )
			{
				cursor = s_toolMove;
			}
			else
			{
				cursor = s_toolDraw;
			}
			break;
		case ModeErase: cursor = s_toolErase; break;
		case ModeSelect: cursor = s_toolSelect; break;
		case ModeEditDetuning: cursor = s_toolOpen; break;
	}
	if( cursor != NULL )
	{
		p.drawPixmap( mapFromGlobal( QCursor::pos() ) + QPoint( 8, 8 ),
								*cursor );
	}

	if( ConfigManager::inst()->value( "ui", "printnotelabels").toInt() )
	{
		printNoteHeights(p, keyAreaBottom(), width(), m_startKey);
	}
}




// responsible for moving/resizing scrollbars after window-resizing
void PianoRoll::resizeEvent(QResizeEvent * re)
{
	m_leftRightScroll->setGeometry( WHITE_KEY_WIDTH,
								      height() -
								SCROLLBAR_SIZE,
					width()-WHITE_KEY_WIDTH,
							SCROLLBAR_SIZE );
	m_topBottomScroll->setGeometry( width() - SCROLLBAR_SIZE, PR_TOP_MARGIN,
						SCROLLBAR_SIZE,
						height() - PR_TOP_MARGIN -
						SCROLLBAR_SIZE );

	int total_pixels = OCTAVE_HEIGHT * NumOctaves - ( height() -
					PR_TOP_MARGIN - PR_BOTTOM_MARGIN -
							m_notesEditHeight );
	m_totalKeysToScroll = total_pixels * KeysPerOctave / OCTAVE_HEIGHT;

	m_topBottomScroll->setRange( 0, m_totalKeysToScroll );

	if( m_startKey > m_totalKeysToScroll )
	{
		m_startKey = m_totalKeysToScroll;
	}
	m_topBottomScroll->setValue( m_totalKeysToScroll - m_startKey );

	Engine::getSong()->getPlayPos( Song::Mode_PlayPattern
					).m_timeLine->setFixedWidth( width() );

	update();
}




void PianoRoll::wheelEvent(QWheelEvent * we )
{
	we->accept();
	// handle wheel events for note edit area - for editing note vol/pan with mousewheel
	if( we->x() > noteEditLeft() && we->x() < noteEditRight()
	&& we->y() > noteEditTop() && we->y() < noteEditBottom() )
	{
		// get values for going through notes
		int pixel_range = 8;
		int x = we->x() - WHITE_KEY_WIDTH;
		int ticks_start = ( x - pixel_range / 2 ) *
					MidiTime::ticksPerTact() / m_ppt + m_currentPosition;
		int ticks_end = ( x + pixel_range / 2 ) *
					MidiTime::ticksPerTact() / m_ppt + m_currentPosition;

		// get note-vector of current pattern
		NoteVector notes;
		notes += m_pattern->notes();

		// go through notes to figure out which one we want to change
		NoteVector nv;
		foreach( Note * i, notes )
		{
			if( i->pos().getTicks() >= ticks_start
				&& i->pos().getTicks() <= ticks_end
				&& i->length().getTicks() != 0
				&& ( i->selected() || ! isSelection() ) )
			{
				nv += i;
			}
		}
		if( nv.size() > 0 )
		{
			const int step = we->delta() > 0 ? 1.0 : -1.0;
			if( m_noteEditMode == NoteEditVolume )
			{
				foreach( Note * n, nv )
				{
					volume_t vol = tLimit<int>( n->getVolume() + step, MinVolume, MaxVolume );
					n->setVolume( vol );
				}
				s_textFloat->setText( tr("Volume: %1%").arg( nv[0]->getVolume() ) );
			}
			else if( m_noteEditMode == NoteEditPanning )
			{
				foreach( Note * n, nv )
				{
					panning_t pan = tLimit<int>( n->getPanning() + step, PanningLeft, PanningRight );
					n->setPanning( pan );
				}
				panning_t pan = nv[0]->getPanning();
				if( pan < 0 )
				{
					s_textFloat->setText( tr("Panning: %1% left").arg( qAbs( pan ) ) );
				}
				else if( pan > 0 )
				{
					s_textFloat->setText( tr("Panning: %1% right").arg( qAbs( pan ) ) );
				}
				else
				{
					s_textFloat->setText( tr("Panning: center") );
				}
			}
			if( nv.size() == 1 )
			{
				s_textFloat->moveGlobal( this,	QPoint( we->x() + 4, we->y() + 16 ) );
				s_textFloat->setVisibilityTimeOut( 1000 );
			}
			update();
		}
	}

	// not in note edit area, so handle scrolling/zooming and quantization change
	else
	if( we->modifiers() & Qt::ControlModifier && we->modifiers() & Qt::AltModifier )
	{
		int q = m_quantizeModel.value();
		if( we->delta() > 0 )
		{
			q--;
		}
		if( we->delta() < 0 )
		{
			q++;
		}
		q = qBound( 0, q, m_quantizeModel.size() - 1 );
		m_quantizeModel.setValue( q );
	}
	else if( we->modifiers() & Qt::ControlModifier && we->modifiers() & Qt::ShiftModifier )
	{
		int l = m_noteLenModel.value();
		if( we->delta() > 0 )
		{
			l--;
		}
		if( we->delta() < 0 )
		{
			l++;
		}
		l = qBound( 0, l, m_noteLenModel.size() - 1 );
		m_noteLenModel.setValue( l );
	}
	else if( we->modifiers() & Qt::ControlModifier )
	{
		int z = m_zoomingModel.value();
		if( we->delta() > 0 )
		{
			z++;
		}
		if( we->delta() < 0 )
		{
			z--;
		}
		z = qBound( 0, z, m_zoomingModel.size() - 1 );
		// update combobox with zooming-factor
		m_zoomingModel.setValue( z );
	}
	else if( we->modifiers() & Qt::ShiftModifier
			 || we->orientation() == Qt::Horizontal )
	{
		m_leftRightScroll->setValue( m_leftRightScroll->value() -
							we->delta() * 2 / 15 );
	}
	else
	{
		m_topBottomScroll->setValue( m_topBottomScroll->value() -
							we->delta() / 30 );
	}
}




int PianoRoll::getKey(int y ) const
{
	int key_line_y = keyAreaBottom() - 1;
	// pressed key on piano
	int key_num = ( key_line_y - y ) / KEY_LINE_HEIGHT;
	key_num += m_startKey;

	// some range-checking-stuff
	if( key_num < 0 )
	{
		key_num = 0;
	}

	if( key_num >= KeysPerOctave * NumOctaves )
	{
		key_num = KeysPerOctave * NumOctaves - 1;
	}

	return key_num;
}




Song::PlayModes PianoRoll::desiredPlayModeForAccompany() const
{
	if( m_pattern->getTrack()->trackContainer() ==
					Engine::getBBTrackContainer() )
	{
		return Song::Mode_PlayBB;
	}
	return Song::Mode_PlaySong;
}




void PianoRoll::play()
{
	if( ! hasValidPattern() )
	{
		return;
	}

	if( Engine::getSong()->playMode() != Song::Mode_PlayPattern )
	{
		Engine::getSong()->playPattern( m_pattern );
	}
	else
	{
		Engine::getSong()->togglePause();
	}
}




void PianoRoll::record()
{
	if( Engine::getSong()->isPlaying() )
	{
		stop();
	}
	if( m_recording || ! hasValidPattern() )
	{
		return;
	}

	m_recording = true;

	Engine::getSong()->playPattern( m_pattern, false );
}




void PianoRoll::recordAccompany()
{
	if( Engine::getSong()->isPlaying() )
	{
		stop();
	}
	if( m_recording || ! hasValidPattern() )
	{
		return;
	}

	m_recording = true;

	if( m_pattern->getTrack()->trackContainer() == Engine::getSong() )
	{
		Engine::getSong()->playSong();
	}
	else
	{
		Engine::getSong()->playBB();
	}
}





void PianoRoll::stop()
{
	Engine::getSong()->stop();
	m_recording = false;
	m_scrollBack = true;
}




void PianoRoll::startRecordNote(const Note & n )
{
	if( m_recording && hasValidPattern() &&
			Engine::getSong()->isPlaying() &&
			(Engine::getSong()->playMode() == desiredPlayModeForAccompany() ||
			 Engine::getSong()->playMode() == Song::Mode_PlayPattern ))
	{
		MidiTime sub;
		if( Engine::getSong()->playMode() == Song::Mode_PlaySong )
		{
			sub = m_pattern->startPosition();
		}
		Note n1( 1, Engine::getSong()->getPlayPos(
					Engine::getSong()->playMode() ) - sub,
				n.key(), n.getVolume(), n.getPanning() );
		if( n1.pos() >= 0 )
		{
			m_recordingNotes << n1;
		}
	}
}




void PianoRoll::finishRecordNote(const Note & n )
{
	if( m_recording && hasValidPattern() &&
		Engine::getSong()->isPlaying() &&
			( Engine::getSong()->playMode() ==
					desiredPlayModeForAccompany() ||
				Engine::getSong()->playMode() ==
					Song::Mode_PlayPattern ) )
	{
		for( QList<Note>::Iterator it = m_recordingNotes.begin();
					it != m_recordingNotes.end(); ++it )
		{
			if( it->key() == n.key() )
			{
				Note n( n.length(), it->pos(),
						it->key(), it->getVolume(),
						it->getPanning() );
				n.quantizeLength( quantization() );
				m_pattern->addNote( n );
				update();
				m_recordingNotes.erase( it );
				break;
			}
		}
	}
}




void PianoRoll::horScrolled(int new_pos )
{
	m_currentPosition = new_pos;
	emit positionChanged( m_currentPosition );
	update();
}




void PianoRoll::verScrolled( int new_pos )
{
	// revert value
	m_startKey = m_totalKeysToScroll - new_pos;

	update();
}




void PianoRoll::setEditMode(int mode)
{
	m_editMode = (EditModes) mode;
}




void PianoRoll::selectAll()
{
	if( ! hasValidPattern() )
	{
		return;
	}

	const NoteVector & notes = m_pattern->notes();

	// if first_time = true, we HAVE to set the vars for select
	bool first_time = true;
	NoteVector::ConstIterator it;

	for( it = notes.begin(); it != notes.end(); ++it )
	{
		Note *note = *it;
		int len_ticks = note->length();

		if( len_ticks > 0 )
		{
			const int key = note->key();

			int pos_ticks = note->pos();
			if( key <= m_selectStartKey || first_time )
			{
				// if we move start-key down, we have to add
				// the difference between old and new start-key
				// to m_selectedKeys, otherwise the selection
				// is just moved down...
				m_selectedKeys += m_selectStartKey
								- ( key - 1 );
				m_selectStartKey = key - 1;
			}
			if( key >= m_selectedKeys+m_selectStartKey ||
								first_time )
			{
				m_selectedKeys = key - m_selectStartKey;
			}
			if( pos_ticks < m_selectStartTick ||
								first_time )
			{
				m_selectStartTick = pos_ticks;
			}
			if( pos_ticks + len_ticks >
				m_selectStartTick + m_selectedTick ||
								first_time )
			{
				m_selectedTick = pos_ticks +
							len_ticks -
							m_selectStartTick;
			}
			first_time = false;
		}
	}
}




// returns vector with pointers to all selected notes
void PianoRoll::getSelectedNotes(NoteVector & selected_notes )
{
	if( ! hasValidPattern() )
	{
		return;
	}

	const NoteVector & notes = m_pattern->notes();
	NoteVector::ConstIterator it;

	for( it = notes.begin(); it != notes.end(); ++it )
	{
		Note *note = *it;
		if( note->selected() )
		{
			selected_notes.push_back( note );
		}
	}
}


void PianoRoll::enterValue( NoteVector* nv )
{

	if( m_noteEditMode == NoteEditVolume )
	{
		bool ok;
		int new_val;
		new_val = QInputDialog::getInt(	this, "Piano roll: note volume",
					tr( "Please enter a new value between %1 and %2:" ).
						arg( MinVolume ).arg( MaxVolume ),
					(*nv)[0]->getVolume(),
					MinVolume, MaxVolume, 1, &ok );

		if( ok )
		{
			foreach( Note * n, *nv )
			{
				n->setVolume( new_val );
			}
			m_lastNoteVolume = new_val;
		}
	}
	else if( m_noteEditMode == NoteEditPanning )
	{
		bool ok;
		int new_val;
		new_val = QInputDialog::getInt(	this, "Piano roll: note panning",
					tr( "Please enter a new value between %1 and %2:" ).
							arg( PanningLeft ).arg( PanningRight ),
						(*nv)[0]->getPanning(),
						PanningLeft, PanningRight, 1, &ok );

		if( ok )
		{
			foreach( Note * n, *nv )
			{
				n->setPanning( new_val );
			}
			m_lastNotePanning = new_val;
		}

	}
}


void PianoRoll::copy_to_clipboard( const NoteVector & notes ) const
{
	DataFile dataFile( DataFile::ClipboardData );
	QDomElement note_list = dataFile.createElement( "note-list" );
	dataFile.content().appendChild( note_list );

	MidiTime start_pos( notes.front()->pos().getTact(), 0 );
	for( NoteVector::ConstIterator it = notes.begin(); it != notes.end();
									++it )
	{
		Note clip_note( **it );
		clip_note.setPos( clip_note.pos( start_pos ) );
		clip_note.saveState( dataFile, note_list );
	}

	QMimeData * clip_content = new QMimeData;
	clip_content->setData( Clipboard::mimeType(), dataFile.toString().toUtf8() );
	QApplication::clipboard()->setMimeData( clip_content,
							QClipboard::Clipboard );
}




void PianoRoll::copySelectedNotes()
{
	NoteVector selected_notes;
	getSelectedNotes( selected_notes );

	if( ! selected_notes.empty() )
	{
		copy_to_clipboard( selected_notes );
	}
}




void PianoRoll::cutSelectedNotes()
{
	if( ! hasValidPattern() )
	{
		return;
	}

	NoteVector selected_notes;
	getSelectedNotes( selected_notes );

	if( ! selected_notes.empty() )
	{
		copy_to_clipboard( selected_notes );

		Engine::getSong()->setModified();
		NoteVector::Iterator it;

		for( it = selected_notes.begin(); it != selected_notes.end(); ++it )
		{
			Note *note = *it;
			// note (the memory of it) is also deleted by
			// pattern::removeNote(...) so we don't have to do that
			m_pattern->removeNote( note );
		}
	}

	update();
	gui->songEditor()->update();
}




void PianoRoll::pasteNotes()
{
	if( ! hasValidPattern() )
	{
		return;
	}

	QString value = QApplication::clipboard()
				->mimeData( QClipboard::Clipboard )
						->data( Clipboard::mimeType() );

	if( ! value.isEmpty() )
	{
		DataFile dataFile( value.toUtf8() );

		QDomNodeList list = dataFile.elementsByTagName( Note::classNodeName() );

		// remove selection and select the newly pasted notes
		clearSelectedNotes();

		if( ! list.isEmpty() )
		{
			m_pattern->addJournalCheckPoint();
		}

		for( int i = 0; ! list.item( i ).isNull(); ++i )
		{
			// create the note
			Note cur_note;
			cur_note.restoreState( list.item( i ).toElement() );
			cur_note.setPos( cur_note.pos() + m_timeLine->pos() );

			// select it
			cur_note.setSelected( true );

			// add to pattern
			m_pattern->addNote( cur_note );
		}

		// we only have to do the following lines if we pasted at
		// least one note...
		Engine::getSong()->setModified();
		update();
		gui->songEditor()->update();
	}
}




void PianoRoll::deleteSelectedNotes()
{
	if( ! hasValidPattern() )
	{
		return;
	}

	bool update_after_delete = false;

	m_pattern->addJournalCheckPoint();

	// get note-vector of current pattern
	const NoteVector & notes = m_pattern->notes();

	// will be our iterator in the following loop
	NoteVector::ConstIterator it = notes.begin();
	while( it != notes.end() )
	{
		Note *note = *it;
		if( note->selected() )
		{
			// delete this note
			m_pattern->removeNote( note );
			update_after_delete = true;

			// start over, make sure we get all the notes
			it = notes.begin();
		}
		else
		{
			++it;
		}
	}

	if( update_after_delete )
	{
		Engine::getSong()->setModified();
		update();
		gui->songEditor()->update();
	}

}




void PianoRoll::autoScroll( const MidiTime & t )
{
	const int w = width() - WHITE_KEY_WIDTH;
	if( t > m_currentPosition + w * MidiTime::ticksPerTact() / m_ppt )
	{
		m_leftRightScroll->setValue( t.getTact() * MidiTime::ticksPerTact() );
	}
	else if( t < m_currentPosition )
	{
		MidiTime t2 = qMax( t - w * MidiTime::ticksPerTact() *
					MidiTime::ticksPerTact() / m_ppt, (tick_t) 0 );
		m_leftRightScroll->setValue( t2.getTact() * MidiTime::ticksPerTact() );
	}
	m_scrollBack = false;
}




void PianoRoll::updatePosition( const MidiTime & t )
{
	if( ( Engine::getSong()->isPlaying()
			&& Engine::getSong()->playMode() == Song::Mode_PlayPattern
			&& m_timeLine->autoScroll() == TimeLineWidget::AutoScrollEnabled
		) || m_scrollBack )
	{
		autoScroll( t );
	}
}




void PianoRoll::updatePositionAccompany( const MidiTime & t )
{
	Song * s = Engine::getSong();

	if( m_recording && hasValidPattern() &&
					s->playMode() != Song::Mode_PlayPattern )
	{
		MidiTime pos = t;
		if( s->playMode() != Song::Mode_PlayBB )
		{
			pos -= m_pattern->startPosition();
		}
		if( (int) pos > 0 )
		{
			s->getPlayPos( Song::Mode_PlayPattern ).setTicks( pos );
			autoScroll( pos );
		}
	}
}




void PianoRoll::zoomingChanged()
{
	const QString & zfac = m_zoomingModel.currentText();
	m_ppt = zfac.left( zfac.length() - 1 ).toInt() * DEFAULT_PR_PPT / 100;

	assert( m_ppt > 0 );

	m_timeLine->setPixelsPerTact( m_ppt );
	update();
}




void PianoRoll::quantizeChanged()
{
	update();
}


int PianoRoll::quantization() const
{
	if( m_quantizeModel.value() == 0 )
	{
		if( m_noteLenModel.value() > 0 )
		{
			return newNoteLen();
		}
		else
		{
			return DefaultTicksPerTact / 16;
		}
	}

	QString text = m_quantizeModel.currentText();
	return DefaultTicksPerTact / text.right( text.length() - 2 ).toInt();
}




void PianoRoll::updateSemiToneMarkerMenu()
{
	const auto& chord_table = InstrumentFunctionNoteStacking::ChordTable::getInstance();
	const InstrumentFunctionNoteStacking::Chord& scale =
			chord_table.getScaleByName( m_scaleModel.currentText() );
	const InstrumentFunctionNoteStacking::Chord& chord =
			chord_table.getChordByName( m_chordModel.currentText() );

	emit semiToneMarkerMenuScaleSetEnabled( ! scale.isEmpty() );
	emit semiToneMarkerMenuChordSetEnabled( ! chord.isEmpty() );
}




MidiTime PianoRoll::newNoteLen() const
{
	if( m_noteLenModel.value() == 0 )
	{
		return m_lenOfNewNotes;
	}

	QString text = m_noteLenModel.currentText();
	return DefaultTicksPerTact / text.right( text.length() - 2 ).toInt();
}




bool PianoRoll::mouseOverNote()
{
	return hasValidPattern() && noteUnderMouse() != NULL;
}




Note * PianoRoll::noteUnderMouse()
{
	QPoint pos = mapFromGlobal( QCursor::pos() );

	// get note-vector of current pattern
	const NoteVector & notes = m_pattern->notes();

	if( pos.x() <= WHITE_KEY_WIDTH
		|| pos.x() > width() - SCROLLBAR_SIZE
		|| pos.y() < PR_TOP_MARGIN
		|| pos.y() > keyAreaBottom() )
	{
		return NULL;
	}

	int key_num = getKey( pos.y() );
	int pos_ticks = ( pos.x() - WHITE_KEY_WIDTH ) *
			MidiTime::ticksPerTact() / m_ppt + m_currentPosition;

	// loop through whole note-vector...
	for( Note* const& note : notes )
	{
		// and check whether the cursor is over an
		// existing note
		if( pos_ticks >= note->pos()
				&& pos_ticks <= note->endPos()
				&& note->key() == key_num
				&& note->length() > 0 )
		{
			return note;
		}
	}

	return NULL;
}




PianoRollWindow::PianoRollWindow() :
	Editor(true),
	m_editor(new PianoRoll())
{
	setCentralWidget(m_editor);

	m_playAction->setToolTip(tr("Play/pause current pattern (Space)"));
	m_recordAction->setToolTip(tr("Record notes from MIDI-device/channel-piano"));
	m_recordAccompanyAction->setToolTip(tr("Record notes from MIDI-device/channel-piano while playing song or BB track"));
	m_stopAction->setToolTip(tr("Stop playing of current pattern (Space)"));

	m_playAction->setWhatsThis(
		tr( "Click here to play the current pattern. "
			"This is useful while editing it. The pattern is "
			"automatically looped when its end is reached." ) );
	m_recordAction->setWhatsThis(
		tr( "Click here to record notes from a MIDI-"
			"device or the virtual test-piano of the according "
			"channel-window to the current pattern. When recording "
			"all notes you play will be written to this pattern "
			"and you can play and edit them afterwards." ) );
	m_recordAccompanyAction->setWhatsThis(
		tr( "Click here to record notes from a MIDI-"
			"device or the virtual test-piano of the according "
			"channel-window to the current pattern. When recording "
			"all notes you play will be written to this pattern "
			"and you will hear the song or BB track in the background." ) );
	m_stopAction->setWhatsThis(
		tr( "Click here to stop playback of current pattern." ) );

	// init edit-buttons at the top
	ActionGroup* editModeGroup = new ActionGroup(this);
	QAction* drawAction = editModeGroup->addAction(embed::getIconPixmap("edit_draw"), tr("Draw mode (Shift+D)"));
	QAction* eraseAction = editModeGroup->addAction(embed::getIconPixmap("edit_erase"), tr("Erase mode (Shift+E)"));
	QAction* selectAction = editModeGroup->addAction(embed::getIconPixmap("edit_select"), tr("Select mode (Shift+S)"));
	QAction* detuneAction = editModeGroup->addAction(embed::getIconPixmap("automation"), tr("Detune mode (Shift+T)"));

	drawAction->setChecked( true );

	drawAction->setShortcut(Qt::SHIFT | Qt::Key_D);
	eraseAction->setShortcut(Qt::SHIFT | Qt::Key_E);
	selectAction->setShortcut(Qt::SHIFT | Qt::Key_S);
	detuneAction->setShortcut(Qt::SHIFT | Qt::Key_T);

	drawAction->setWhatsThis(
		tr( "Click here and draw mode will be activated. In this "
			"mode you can add, resize and move notes. This "
			"is the default mode which is used most of the time. "
			"You can also press 'Shift+D' on your keyboard to "
			"activate this mode. In this mode, hold Ctrl to "
			"temporarily go into select mode." ) );
	eraseAction->setWhatsThis(
		tr( "Click here and erase mode will be activated. In this "
			"mode you can erase notes. You can also press "
			"'Shift+E' on your keyboard to activate this mode." ) );
	selectAction->setWhatsThis(
		tr( "Click here and select mode will be activated. "
			"In this mode you can select notes. Alternatively, "
			"you can hold Ctrl in draw mode to temporarily use "
			"select mode." ) );
	detuneAction->setWhatsThis(
		tr( "Click here and detune mode will be activated. "
			"In this mode you can click a note to open its "
			"automation detuning. You can utilize this to slide "
			"notes from one to another. You can also press "
			"'Shift+T' on your keyboard to activate this mode." ) );

	connect(editModeGroup, SIGNAL(triggered(int)), m_editor, SLOT(setEditMode(int)));

	// Copy + paste actions
	QAction* cutAction = new QAction(embed::getIconPixmap("edit_cut"),
							  tr("Cut selected notes (Ctrl+X)"), this);

	QAction* copyAction = new QAction(embed::getIconPixmap("edit_copy"),
							   tr("Copy selected notes (Ctrl+C)"), this);

	QAction* pasteAction = new QAction(embed::getIconPixmap("edit_paste"),
					tr("Paste notes from clipboard (Ctrl+V)"), this);

	cutAction->setWhatsThis(
		tr( "Click here and the selected notes will be cut into the "
			"clipboard. You can paste them anywhere in any pattern "
			"by clicking on the paste button." ) );
	copyAction->setWhatsThis(
		tr( "Click here and the selected notes will be copied into the "
			"clipboard. You can paste them anywhere in any pattern "
			"by clicking on the paste button." ) );
	pasteAction->setWhatsThis(
		tr( "Click here and the notes from the clipboard will be "
			"pasted at the first visible measure." ) );

	cutAction->setShortcut(Qt::CTRL | Qt::Key_X);
	copyAction->setShortcut(Qt::CTRL | Qt::Key_C);
	pasteAction->setShortcut(Qt::CTRL | Qt::Key_V);

	connect(cutAction, SIGNAL(triggered()), m_editor, SLOT(cutSelectedNotes()));
	connect(copyAction, SIGNAL(triggered()), m_editor, SLOT(copySelectedNotes()));
	connect(pasteAction, SIGNAL(triggered()), m_editor, SLOT(pasteNotes()));

	QLabel * zoom_lbl = new QLabel( m_toolBar );
	zoom_lbl->setPixmap( embed::getIconPixmap( "zoom" ) );

	m_zoomingComboBox = new ComboBox( m_toolBar );
	m_zoomingComboBox->setModel( &m_editor->m_zoomingModel );
	m_zoomingComboBox->setFixedSize( 64, 22 );

	// setup quantize-stuff
	QLabel * quantize_lbl = new QLabel( m_toolBar );
	quantize_lbl->setPixmap( embed::getIconPixmap( "quantize" ) );

	m_quantizeComboBox = new ComboBox( m_toolBar );
	m_quantizeComboBox->setModel( &m_editor->m_quantizeModel );
	m_quantizeComboBox->setFixedSize( 64, 22 );


	// setup note-len-stuff
	QLabel * note_len_lbl = new QLabel( m_toolBar );
	note_len_lbl->setPixmap( embed::getIconPixmap( "note" ) );


	m_noteLenComboBox = new ComboBox( m_toolBar );
	m_noteLenComboBox->setModel( &m_editor->m_noteLenModel );
	m_noteLenComboBox->setFixedSize( 105, 22 );

	// setup scale-stuff
	QLabel * scale_lbl = new QLabel( m_toolBar );
	scale_lbl->setPixmap( embed::getIconPixmap( "scale" ) );

	m_scaleComboBox = new ComboBox( m_toolBar );
	m_scaleComboBox->setModel( &m_editor->m_scaleModel );
	m_scaleComboBox->setFixedSize( 105, 22 );

	// setup chord-stuff
	QLabel * chord_lbl = new QLabel( m_toolBar );
	chord_lbl->setPixmap( embed::getIconPixmap( "chord" ) );

	m_chordComboBox = new ComboBox( m_toolBar );
	m_chordComboBox->setModel( &m_editor->m_chordModel );
	m_chordComboBox->setFixedSize( 105, 22 );


	m_toolBar->addSeparator();
	m_toolBar->addAction( drawAction );
	m_toolBar->addAction( eraseAction );
	m_toolBar->addAction( selectAction );
	m_toolBar->addAction( detuneAction );

	m_toolBar->addSeparator();
	m_toolBar->addAction( cutAction );
	m_toolBar->addAction( copyAction );
	m_toolBar->addAction( pasteAction );

	m_toolBar->addSeparator();
	m_editor->m_timeLine->addToolButtons(m_toolBar);

	m_toolBar->addSeparator();
	m_toolBar->addWidget( zoom_lbl );
	m_toolBar->addWidget( m_zoomingComboBox );

	m_toolBar->addSeparator();
	m_toolBar->addWidget( quantize_lbl );
	m_toolBar->addWidget( m_quantizeComboBox );

	m_toolBar->addSeparator();
	m_toolBar->addWidget( note_len_lbl );
	m_toolBar->addWidget( m_noteLenComboBox );

	m_toolBar->addSeparator();
	m_toolBar->addWidget( scale_lbl );
	m_toolBar->addWidget( m_scaleComboBox );

	m_toolBar->addSeparator();
	m_toolBar->addWidget( chord_lbl );
	m_toolBar->addWidget( m_chordComboBox );

	m_zoomingComboBox->setWhatsThis(
				tr(
					"This controls the magnification of an axis. "
					"It can be helpful to choose magnification for a specific "
					"task. For ordinary editing, the magnification should be "
					"fitted to your smallest notes. "
					) );

	m_quantizeComboBox->setWhatsThis(
				tr(
					"The 'Q' stands for quantization, and controls the grid size "
					"notes and control points snap to. "
					"With smaller quantization values, you can draw shorter notes "
					"in Piano Roll, and more exact control points in the "
					"Automation Editor."

					) );

	m_noteLenComboBox->setWhatsThis(
				tr(
					"This lets you select the length of new notes. "
					"'Last Note' means that LMMS will use the note length of "
					"the note you last edited"
					) );

	m_scaleComboBox->setWhatsThis(
				tr(
					"The feature is directly connected to the context-menu "
					"on the virtual keyboard, to the left in Piano Roll. "
					"After you have chosen the scale you want "
					"in this drop-down menu, "
					"you can right click on a desired key in the virtual keyboard, "
					"and then choose 'Mark current Scale'. "
					"LMMS will highlight all notes that belongs to the chosen scale, "
					"and in the key you have selected!"
					) );


	m_chordComboBox->setWhatsThis(
				tr(
					"Let you select a chord which LMMS then can draw or highlight."
					"You can find the most common chords in this drop-down menu. "
					"After you have selected a chord, click anywhere to place the chord, and right "
					"click on the virtual keyboard to open context menu and highlight the chord. "
					"To return to single note placement, you need to choose 'No chord' "
					"in this drop-down menu."
					) );


	// setup our actual window
	setFocusPolicy( Qt::StrongFocus );
	setFocus();
	setWindowIcon( embed::getIconPixmap( "piano" ) );
	setCurrentPattern( NULL );

	// Connections
	connect(m_editor, SIGNAL(currentPatternChanged()), this, SIGNAL(currentPatternChanged()));
}

const Pattern* PianoRollWindow::currentPattern() const
{
	return m_editor->currentPattern();
}

void PianoRollWindow::setCurrentPattern(Pattern* pattern)
{
	m_editor->setCurrentPattern(pattern);
}

bool PianoRollWindow::isRecording() const
{
	return m_editor->isRecording();
}

int PianoRollWindow::quantization() const
{
	return m_editor->quantization();
}

void PianoRollWindow::play()
{
	m_editor->play();
}

void PianoRollWindow::stop()
{
	m_editor->stop();
}

void PianoRollWindow::record()
{
	m_editor->record();
}

void PianoRollWindow::recordAccompany()
{
	m_editor->recordAccompany();
}

void PianoRollWindow::stopRecording()
{
	m_editor->stopRecording();
}

void PianoRollWindow::reset()
{
	m_editor->reset();
}


void PianoRollWindow::saveSettings(QDomDocument & doc, QDomElement & de)
{
	MainWindow::saveWidgetState(this, de);
}

void PianoRollWindow::loadSettings(const QDomElement & de)
{
	MainWindow::restoreWidgetState(this, de);
}

QSize PianoRollWindow::sizeHint() const
{
	return {m_toolBar->sizeHint().width() + 10, INITIAL_PIANOROLL_HEIGHT};
}