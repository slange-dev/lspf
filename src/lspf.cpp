/* Compile with ::                                                                                                                                           */
/* g++ -O0 -std=c++11 -rdynamic -Wunused-variable -ltinfo -lncurses -lpanel -lboost_thread -lboost_filesystem -lboost_system -ldl -lpthread -o lspf lspf.cpp */

/*
  Copyright (c) 2015 Daniel John Erdos

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

#include "lspf.h"

#include <ncurses.h>
#include <dlfcn.h>
#include <sys/utsname.h>

#include <locale>
#include <boost/date_time.hpp>

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/filesystem.hpp>

boost::condition cond_appl ;

#include "utilities.h"
#include "utilities.cpp"

#include "colours.h"

#include "classes.h"
#include "classes.cpp"

#include "pVPOOL.h"
#include "pVPOOL.cpp"

#include "pWidgets.h"
#include "pWidgets.cpp"

#include "pTable.h"
#include "pTable.cpp"

#include "pPanel.h"
#include "pPanel1.cpp"
#include "pPanel2.cpp"

#include "pApplication.h"
#include "pApplication.cpp"

#include "ispexeci.cpp"

#include "pLScreen.h"
#include "pLScreen.cpp"

#undef  MOD_NAME
#define MOD_NAME lspf

#define currScrn pLScreen::currScreen
#define OIA      pLScreen::OIA

#define CTRL(c) ((c) & 037)

namespace {
map<int, string> pfKeyDefaults = { {  1, "HELP"      },
				   {  2, "SPLIT NEW" },
				   {  3, "END"       },
				   {  4, "RETURN"    },
				   {  5, "RFIND"     },
				   {  6, "RCHANGE"   },
				   {  7, "UP"        },
				   {  8, "DOWN"      },
				   {  9, "SWAP"      },
				   { 10, "LEFT"      },
				   { 11, "RIGHT"     },
				   { 12, "RETRIEVE"  },
				   { 13, "HELP"      },
				   { 14, "SPLIT NEW" },
				   { 15, "END"       },
				   { 16, "RETURN"    },
				   { 17, "RFIND"     },
				   { 18, "RCHANGE"   },
				   { 19, "UP"        },
				   { 20, "DOWN"      },
				   { 21, "SWAP"      },
				   { 22, "SWAP PREV" },
				   { 23, "SWAP NEXT" },
				   { 24, "HELP"      } } ;

pApplication* currAppl ;

poolMGR*  p_poolMGR  = new poolMGR  ;
tableMGR* p_tableMGR = new tableMGR ;
logger*   lg         = new logger   ;
logger*   lgx        = new logger   ;

fPOOL funcPOOL ;

vector<pLScreen*> screenList ;

struct appInfo
{
	string file       ;
	string module     ;
	void* dlib        ;
	void* maker_ep    ;
	void* destroyer_ep ;
	bool  mainpgm     ;
	bool  dlopened    ;
	bool  errors      ;
	bool  relPending  ;
	int   refCount    ;
} ;

map<string, appInfo> apps ;

boost::circular_buffer<string> retrieveBuffer( 99 ) ;

int    linePosn  = -1 ;
int    maxtaskID = 0  ;
uint   retPos    = 0  ;
uint   priScreen = 0  ;
uint   altScreen = 0  ;
uint   intens    = 0  ;
string ctlAction      ;
string commandStack   ;
string jumpOption     ;
bool   pfkeyPressed   ;
bool   ctlkeyPressed  ;
bool   wmPending      ;

vector<pApplication*> pApplicationBackground ;
vector<pApplication*> pApplicationTimeout    ;

errblock err    ;

string zcommand ;
string zparm    ;

string zctverb  ;
string zcttrunc ;
string zctact   ;
string zctdesc  ;

const char zscreen[] = { '1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G',
			 'H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W' } ;

selobj selct ;

int    gmaxwait ;
string gmainpgm ;

enum LSPF_STATUS
{
     LSPF_STARTING,
     LSPF_RUNNING,
     LSPF_STOPPING
} ;

enum BACK_STATUS
{
     BACK_RUNNING,
     BACK_STOPPING,
     BACK_STOPPED
} ;

enum ZCMD_TYPES
{
	ZC_ACTIONS,
	ZC_DISCARD,
	ZC_FIELDEXC,
	ZC_MSGID,
	ZC_NOP,
	ZC_PANELID,
	ZC_REFRESH,
	ZC_RESIZE,
	ZC_RETP,
	ZC_SCRNAME,
	ZC_SPLIT,
	ZC_SWAP,
	ZC_TDOWN,
	ZC_USERID,
	ZC_DOT_ABEND,
	ZC_DOT_AUTO,
	ZC_DOT_HIDE,
	ZC_DOT_INFO,
	ZC_DOT_LOAD,
	ZC_DOT_RELOAD,
	ZC_DOT_SCALE,
	ZC_DOT_SHELL,
	ZC_DOT_SHOW,
	ZC_DOT_SNAP,
	ZC_DOT_STATS,
	ZC_DOT_TASKS,
	ZC_DOT_TEST
} ;

map<string, ZCMD_TYPES> zcommands =
{ { "ACTIONS",  ZC_ACTIONS    },
  { "DISCARD",  ZC_DISCARD    },
  { "FIELDEXC", ZC_FIELDEXC   },
  { "MSGID",    ZC_MSGID      },
  { "NOP",      ZC_NOP        },
  { "PANELID",  ZC_PANELID    },
  { "REFRESH",  ZC_REFRESH    },
  { "RESIZE",   ZC_RESIZE     },
  { "RETP",     ZC_RETP       },
  { "SCRNAME",  ZC_SCRNAME    },
  { "SPLIT",    ZC_SPLIT      },
  { "SWAP",     ZC_SWAP       },
  { "TDOWN",    ZC_TDOWN      },
  { "USERID",   ZC_USERID     },
  { ".ABEND",   ZC_DOT_ABEND  },
  { ".AUTO",    ZC_DOT_AUTO   },
  { ".HIDE",    ZC_DOT_HIDE   },
  { ".INFO",    ZC_DOT_INFO   },
  { ".LOAD",    ZC_DOT_LOAD   },
  { ".RELOAD",  ZC_DOT_RELOAD },
  { ".SCALE",   ZC_DOT_SCALE  },
  { ".SHELL",   ZC_DOT_SHELL  },
  { ".SHOW",    ZC_DOT_SHOW   },
  { ".SNAP",    ZC_DOT_SNAP   },
  { ".STATS",   ZC_DOT_STATS  },
  { ".TASKS",   ZC_DOT_TASKS  },
  { ".TEST",    ZC_DOT_TEST   }  } ;

LSPF_STATUS lspfStatus ;
BACK_STATUS backStatus ;

boost::recursive_mutex mtx ;

}

unsigned int pLScreen::screensTotal = 0 ;
unsigned int pLScreen::maxScreenId  = 0 ;
unsigned int pLScreen::maxrow       = 0 ;
unsigned int pLScreen::maxcol       = 0 ;

set<int> pLScreen::screenNums ;
map<int,int> pLScreen::openedByList ;

WINDOW* pLScreen::OIA       = NULL ;
PANEL*  pLScreen::OIA_panel = NULL ;

pLScreen*  pLScreen::currScreen = NULL ;

tableMGR* pApplication::p_tableMGR = NULL ;
poolMGR*  pApplication::p_poolMGR  = NULL ;
logger*   pApplication::lg  = NULL ;
poolMGR*  pPanel::p_poolMGR = NULL ;
poolMGR*  abc::p_poolMGR    = NULL ;
logger*   pPanel::lg        = NULL ;
logger*   tableMGR::lg      = NULL ;
logger*   poolMGR::lg       = NULL ;

char  field::field_paduchar = ' ' ;
bool  field::field_nulls    = false ;
uint  pPanel::panel_intens  = 0   ;
uint  field::field_intens   = 0   ;
uint  text::text_intens     = 0   ;
uint  pdc::pdc_intens       = 0   ;
uint  abc::abc_intens       = 0   ;
uint  Box::box_intens       = 0   ;

void setGlobalClassVars() ;
void initialSetup()       ;
void loadDefaultPools()   ;
void getDynamicClasses()  ;
bool loadDynamicClass( const string& ) ;
bool unloadDynamicClass( void* )    ;
void reloadDynamicClasses( string ) ;
void loadSystemCommandTable() ;
void loadRetrieveBuffer() ;
void saveRetrieveBuffer() ;
void cleanup()            ;
void loadCUATables()      ;
void setGlobalColours()   ;
void setColourPair( const string& ) ;
void lScreenDefaultSettings() ;
void updateDefaultVars()      ;
void createSharedPoolVars( const string& ) ;
void updateReflist()          ;
void startApplication( selobj&, bool =false ) ;
void startApplicationBack( selobj, bool =false ) ;
void terminateApplication()     ;
void abnormalTermMessage()      ;
void processBackgroundTasks()   ;
void ResumeApplicationAndWait() ;
bool createLogicalScreen()      ;
void deleteLogicalScreen()      ;
void resolvePGM()               ;
void processPGMSelect()         ;
void processZSEL()              ;
void processAction( uint row, uint col, int c, bool& doSelect, bool& passthru ) ;
void processZCOMMAND( uint row, uint col, bool doSelect ) ;
void issueMessage( const string& ) ;
void lineOutput()     ;
void lineOutput_end() ;
void errorScreen( int, const string& ) ;
void errorScreen( const string&, const string& ) ;
void errorScreen( const string& ) ;
void abortStartup()  ;
void lspfCallbackHandler( lspfCommand& ) ;
void createpfKeyDefaults() ;
string getEnvironmentVariable( const char* ) ;
string pfKeyValue( int )   ;
string ControlKeyAction( char c ) ;
string listLogicalScreens() ;
void actionSwap( uint&, uint& )   ;
void actionTabKey( uint&, uint& ) ;
void displayNextPullDown( uint&, uint& ) ;
void executeFieldCommand( const string&, const fieldExc&, uint ) ;
void listBackTasks()      ;
void autoUpdate()         ;
bool resolveZCTEntry( string&, string& ) ;
bool isActionKey( int c ) ;
void listRetrieveBuffer() ;
int  getScreenNameNum( const string& ) ;
void serviceCallError( errblock& ) ;
void listErrorBlock( errblock& ) ;
void mainLoop() ;



int main( void )
{
	int elapsed ;

	setGlobalClassVars() ;

	lg->open()  ;
	lgx->open() ;

	err.clear() ;

	boost::thread* pThread ;
	boost::thread* bThread ;

	commandStack = "" ;
	wmPending    = false ;
	lspfStatus   = LSPF_STARTING ;
	backStatus   = BACK_STOPPED  ;
	err.settask( 1 ) ;

	screenList.push_back( new pLScreen( 0, ZMAXSCRN ) ) ;

	currScrn->OIA_startTime() ;

	llog( "I", "lspf version " LSPF_VERSION " startup in progress" << endl ) ;

	llog( "I", "Calling initialSetup" << endl ) ;
	initialSetup() ;

	llog( "I", "Calling loadDefaultPools" << endl ) ;
	loadDefaultPools() ;

	lspfStatus = LSPF_RUNNING ;

	llog( "I", "Starting background job monitor task" << endl ) ;
	bThread = new boost::thread( &processBackgroundTasks ) ;

	lScreenDefaultSettings() ;

	llog( "I", "Calling getDynamicClasses" << endl ) ;
	getDynamicClasses() ;

	llog( "I", "Loading main "+ gmainpgm +" application" << endl ) ;
	if ( not loadDynamicClass( gmainpgm ) )
	{
		llog( "C", "Main program "+ gmainpgm +" cannot be loaded or symbols resolved." << endl ) ;
		cleanup() ;
		delete bThread ;
		return 0  ;
	}

	llog( "I", "Calling loadCUATables" << endl ) ;
	loadCUATables() ;

	llog( "I", "Calling loadSystemCommandTable" << endl ) ;
	loadSystemCommandTable() ;

	loadRetrieveBuffer() ;

	updateDefaultVars() ;

	llog( "I", "Starting new "+ gmainpgm +" thread" << endl ) ;
	currAppl = ((pApplication*(*)())( apps[ gmainpgm ].maker_ep))() ;

	currScrn->application_add( currAppl ) ;

	selct.def( gmainpgm ) ;

	currAppl->init_phase1( selct, ++maxtaskID, lspfCallbackHandler ) ;
	currAppl->shrdPool = 1 ;

	p_poolMGR->createProfilePool( err, "ISP" ) ;
	p_poolMGR->connect( currAppl->taskid(), "ISP", 1 ) ;
	if ( err.RC4() ) { createpfKeyDefaults() ; }

	createSharedPoolVars( "ISP" ) ;
	currAppl->init_phase2() ;

	pThread = new boost::thread( &pApplication::run, currAppl ) ;
	currAppl->pThread = pThread ;
	apps[ gmainpgm ].refCount++ ;
	apps[ gmainpgm ].mainpgm = true ;

	llog( "I", "Waiting for "+ gmainpgm +" to complete startup" << endl ) ;
	elapsed = 0 ;
	while ( currAppl->busyAppl )
	{
		if ( not currAppl->noTimeOut ) { elapsed++ ; }
		boost::this_thread::sleep_for( boost::chrono::milliseconds( ZWAIT ) ) ;
		if ( elapsed > gmaxwait ) { currAppl->set_timeout_abend() ; }
	}
	if ( currAppl->terminateAppl )
	{
		errorScreen( 1, "An error has occured initialising the first "+ gmainpgm +" main task." ) ;
		llog( "C", "Main program "+ gmainpgm +" failed to initialise" << endl ) ;
		currAppl->info() ;
		p_poolMGR->disconnect( currAppl->taskid() ) ;
		llog( "I", "Removing application instance of "+ currAppl->get_appname() << endl ) ;
		((void (*)(pApplication*))(apps[ currAppl->get_appname() ].destroyer_ep))( currAppl ) ;
		delete pThread ;
		cleanup() ;
		delete bThread ;
		return 0  ;
	}

	llog( "I", "First thread "+ gmainpgm +" started and initialised.  ID=" << pThread->get_id() << endl ) ;

	currScrn->set_cursor( currAppl ) ;

	mainLoop() ;

	saveRetrieveBuffer() ;

	llog( "I", "Stopping background job monitor task" << endl ) ;
	lspfStatus = LSPF_STOPPING ;
	backStatus = BACK_STOPPING ;
	while ( backStatus != BACK_STOPPED )
	{
		boost::this_thread::sleep_for( boost::chrono::milliseconds( ZWAIT ) ) ;
	}

	delete bThread ;

	llog( "I", "Closing ISPS profile as last application program has terminated" << endl ) ;
	p_poolMGR->destroySystemPool( err ) ;

	p_poolMGR->statistics()  ;
	p_tableMGR->statistics() ;

	delete p_poolMGR  ;
	delete p_tableMGR ;

	llog( "I", "Closing application log" << endl ) ;
	delete lgx ;

	for ( auto it = apps.begin() ; it != apps.end() ; it++ )
	{
		if ( it->second.dlopened )
		{
			llog( "I", "dlclose of "+ it->first +" at " << it->second.dlib << endl ) ;
			unloadDynamicClass( it->second.dlib ) ;
		}
	}

	llog( "I", "lspf and LOG terminating" << endl ) ;
	delete lg ;

	return 0  ;
}


void cleanup()
{
	// Cleanup resources for early termination and inform the user, including log names.

	delete p_poolMGR  ;
	delete p_tableMGR ;
	delete currScrn   ;

	llog( "I", "Stopping background job monitor task" << endl ) ;
	lspfStatus = LSPF_STOPPING ;
	backStatus = BACK_STOPPING ;
	while ( backStatus != BACK_STOPPED )
	{
		boost::this_thread::sleep_for( boost::chrono::milliseconds( ZWAIT ) ) ;
	}

	cout << "*********************************************************************" << endl ;
	cout << "*********************************************************************" << endl ;
	cout << "Aborting startup of lspf.  Check lspf and application logs for errors" << endl ;
	cout << "lspf log name. . . . . :"<< lg->logname() << endl ;
	cout << "Application log name . :"<< lgx->logname() << endl ;
	cout << "*********************************************************************" << endl ;
	cout << "*********************************************************************" << endl ;
	cout << endl ;

	llog( "I", "lspf and LOG terminating" << endl ) ;
	delete lgx ;
	delete lg  ;
}


void mainLoop()
{
	llog( "I", "mainLoop() entered" << endl ) ;

	uint c   ;
	uint row ;
	uint col ;

	bool passthru ;
	bool showLock ;
	bool Insert   ;
	bool doSelect ;

	MEVENT event ;

	err.clear()  ;

	currScrn->OIA_setup() ;

	mousemask( ALL_MOUSE_EVENTS, NULL ) ;
	showLock = false ;
	Insert   = false ;

	set_escdelay( 25 ) ;

	while ( pLScreen::screensTotal > 0 )
	{
		currScrn->clear_status() ;
		currScrn->get_cursor( row, col ) ;

		currScrn->OIA_update( priScreen, altScreen ) ;
		currScrn->show_lock( showLock ) ;

		showLock      = false ;
		pfkeyPressed  = false ;
		ctlkeyPressed = false ;
		ctlAction     = ""    ;

		if ( commandStack == ""            &&
		     !currAppl->ControlDisplayLock &&
		     !currAppl->line_output_done() &&
		     !currAppl->ControlNonDispl )
		{
			wnoutrefresh( stdscr ) ;
			wnoutrefresh( OIA ) ;
			update_panels() ;
			move( row, col ) ;
			doupdate()  ;
			c = getch() ;
			if ( c == 13 ) { c = KEY_ENTER ; }
		}
		else
		{
			if ( currAppl->ControlDisplayLock )
			{
				wnoutrefresh( stdscr ) ;
				wnoutrefresh( OIA ) ;
				update_panels()  ;
				move( row, col ) ;
				doupdate()  ;
			}
			c = KEY_ENTER ;
		}
		currScrn->OIA_startTime() ;

		if ( c < 256 && isprint( c ) )
		{
			if ( currAppl->inputInhibited() ) { continue ; }
			currAppl->currPanel->field_edit( err, row, col, char( c ), Insert, showLock ) ;
			currScrn->set_cursor( currAppl ) ;
			continue ;
		}

		if ( c == KEY_MOUSE && getmouse( &event ) == OK )
		{
			if ( event.bstate & BUTTON1_CLICKED || event.bstate & BUTTON1_DOUBLE_CLICKED )
			{
				row = event.y ;
				col = event.x ;
				if ( currAppl->currPanel->pd_active() )
				{
					currAppl->currPanel->display_current_pd( err, row, col ) ;
				}
				currScrn->set_cursor( row, col ) ;
				if ( event.bstate & BUTTON1_DOUBLE_CLICKED )
				{
					c = KEY_ENTER ;
				}
			}
		}

		if ( c >= CTRL( 'a' ) && c <= CTRL( 'z' ) && c != CTRL( 'i' ) )
		{
			ctlAction = ControlKeyAction( c ) ;
			ctlkeyPressed = true ;
			c = KEY_ENTER ;
		}

		switch( c )
		{

			case KEY_LEFT:
				currScrn->cursor_left() ;
				if ( currAppl->currPanel->pd_active() )
				{
					col = currScrn->get_col() ;
					currAppl->currPanel->display_current_pd( err, row, col ) ;
				}
				break ;

			case KEY_RIGHT:
				currScrn->cursor_right() ;
				if ( currAppl->currPanel->pd_active() )
				{
					col = currScrn->get_col() ;
					currAppl->currPanel->display_current_pd( err, row, col ) ;
				}
				break ;

			case KEY_UP:
				currScrn->cursor_up() ;
				if ( currAppl->currPanel->pd_active() )
				{
					row = currScrn->get_row() ;
					currAppl->currPanel->display_current_pd( err, row, col ) ;
				}
				break ;

			case KEY_DOWN:
				currScrn->cursor_down() ;
				if ( currAppl->currPanel->pd_active() )
				{
					row = currScrn->get_row() ;
					currAppl->currPanel->display_current_pd( err, row, col ) ;
				}
				break ;

			case CTRL( 'i' ):   // Tab key
				actionTabKey( row, col ) ;
				break ;

			case KEY_IC:
				Insert = not Insert ;
				currScrn->set_Insert( Insert ) ;
				break ;

			case KEY_HOME:
				if ( currAppl->currPanel->pd_active() )
				{
					currAppl->currPanel->get_pd_home( row, col ) ;
					currAppl->currPanel->display_current_pd( err, row, col ) ;
				}
				else
				{
					currAppl->get_home( row, col ) ;
				}
				currScrn->set_cursor( row, col ) ;
				break ;

			case KEY_DC:
				if ( currAppl->inputInhibited() ) { break ; }
				currAppl->currPanel->field_delete_char( err, row, col, showLock ) ;
				break ;

			case KEY_SDC:
				if ( currAppl->inputInhibited() ) { break ; }
				currAppl->currPanel->field_erase_eof( err, row, col, showLock ) ;
				break ;

			case KEY_END:
				currAppl->currPanel->cursor_eof( row, col ) ;
				currScrn->set_cursor( row, col ) ;
				break ;

			case 127:
			case KEY_BACKSPACE:
				if ( currAppl->inputInhibited() ) { break ; }
				currAppl->currPanel->field_backspace( err, row, col, showLock ) ;
				currScrn->set_cursor( row, col ) ;
				break ;

			// All action keys follow
			case KEY_F(1):  case KEY_F(2):  case KEY_F(3):  case KEY_F(4):  case KEY_F(5):  case KEY_F(6):
			case KEY_F(7):  case KEY_F(8):  case KEY_F(9):  case KEY_F(10): case KEY_F(11): case KEY_F(12):
			case KEY_F(13): case KEY_F(14): case KEY_F(15): case KEY_F(16): case KEY_F(17): case KEY_F(18):
			case KEY_F(19): case KEY_F(20): case KEY_F(21): case KEY_F(22): case KEY_F(23): case KEY_F(24):
				pfkeyPressed = true ;

			case KEY_NPAGE:
			case KEY_PPAGE:
			case KEY_ENTER:
			case CTRL( '[' ):       // Escape key

				debug1( "Action key pressed.  Processing" << endl ) ;
				if ( currAppl->msgInhibited() )
				{
					currAppl->msgResponseOK() ;
					break ;
				}
				Insert = false ;
				currScrn->set_Insert( Insert ) ;
				currScrn->show_busy() ;
				updateDefaultVars()   ;
				processAction( row, col, c, doSelect, passthru ) ;
				if ( passthru )
				{
					updateReflist() ;
					currAppl->set_cursor( row, col ) ;
					ResumeApplicationAndWait()       ;
					if ( currAppl->selectPanel() )
					{
						processZSEL() ;
					}
					currScrn->set_cursor( currAppl ) ;
					while ( currAppl->terminateAppl )
					{
						terminateApplication() ;
						if ( pLScreen::screensTotal == 0 ) { return ; }
						if ( currAppl->SEL && !currAppl->terminateAppl )
						{
							processPGMSelect() ;
						}
					}
				}
				else
				{
					processZCOMMAND( row, col, doSelect ) ;
				}
				break ;

			default:
				debug1( "Action key "<<c<<" ("<<keyname( c )<<") ignored" << endl ) ;
		}
	}
}


void setGlobalClassVars()
{
	pApplication::p_tableMGR = p_tableMGR ;
	pApplication::p_poolMGR  = p_poolMGR  ;
	pApplication::lg  = lgx       ;
	pPanel::p_poolMGR = p_poolMGR ;
	abc::p_poolMGR    = p_poolMGR ;
	pPanel::lg        = lgx       ;
	tableMGR::lg      = lgx       ;
	poolMGR::lg       = lgx       ;
}


void initialSetup()
{
	err.clear() ;

	funcPOOL.define( err, "ZCTVERB",  &zctverb  ) ;
	funcPOOL.define( err, "ZCTTRUNC", &zcttrunc ) ;
	funcPOOL.define( err, "ZCTACT",   &zctact   ) ;
	funcPOOL.define( err, "ZCTDESC",  &zctdesc  ) ;
}


void processZSEL()
{
	// Called for a selection panel (ie. SELECT PANEL(ABC) function ).
	// Use what's in ZSEL to start application

	int p ;

	string cmd ;
	string opt ;

	bool addpop = false ;

	err.clear() ;

	currAppl->save_errblock()  ;
	cmd = currAppl->get_zsel() ;
	err = currAppl->get_errblock() ;
	currAppl->restore_errblock()   ;
	if ( err.error() )
	{
		serviceCallError( err ) ;
	}

	if ( cmd == "" ) { return ; }

	if ( cmd.compare( 0, 5, "PANEL" ) == 0 )
	{
		opt = currAppl->get_dTRAIL() ;
		if ( opt != "" ) { cmd += " OPT(" + opt + ")" ; }
	}

	p = wordpos( "ADDPOP", cmd ) ;
	if ( p > 0 )
	{
		addpop = true ;
		idelword( cmd, p, 1 ) ;
	}

	updateDefaultVars() ;
	currAppl->currPanel->remove_pd() ;

	if ( !selct.parse( err, cmd ) )
	{
		errorScreen( "Error in selection panel "+ currAppl->get_panelid(), "ZSEL = "+ cmd ) ;
		return ;
	}

	resolvePGM() ;

	if ( addpop )
	{
		selct.parm += " ADDPOP" ;
	}

	if ( selct.backgrd )
	{
		startApplicationBack( selct ) ;
	}
	else
	{
		selct.quiet = true ;
		startApplication( selct ) ;
		if ( selct.errors )
		{
			p_poolMGR->put( err, "ZVAL1", selct.pgm ,SHARED ) ;
			p_poolMGR->put( err, "ZERRDSC", "P", SHARED ) ;
			p_poolMGR->put( err, "ZERRSRC", "ZSEL = "+ cmd ,SHARED ) ;
			errorScreen( "PSYS012W" ) ;
		}
	}
}


void processAction( uint row, uint col, int c, bool& doSelect, bool& passthru )
{
	// Return if application is just doing line output
	// perform lspf high-level functions - pfkey -> command
	// application/user/site/system command table entry?
	// BUILTIN command
	// System command
	// RETRIEVE/RETF
	// Jump command entered
	// !abc run abc as a program
	// @abc run abc as a REXX procedure
	// Else pass event to application

	int RC ;

	size_t p1 ;

	uint rw ;
	uint cl ;
	uint rtsize ;
	uint rbsize ;

	bool addRetrieve ;

	string cmdVerb ;
	string cmdParm ;
	string pfcmd   ;
	string delm    ;
	string aVerb   ;
	string msg     ;
	string t       ;

	boost::circular_buffer<string>::iterator itt ;

	err.clear() ;

	pdc t_pdc ;

	doSelect = false ;
	passthru = true  ;
	pfcmd    = ""    ;
	zcommand = ""    ;

	if ( currAppl->line_output_done() ) { return ; }

	p_poolMGR->put( err, "ZVERB", "", SHARED ) ;
	if ( err.error() )
	{
		llog( "C", "poolMGR put for ZVERB failed" << endl ) ;
		listErrorBlock( err ) ;
	}

	if ( c == CTRL( '[' ) )
	{
		if ( currAppl->currPanel->pd_active() )
		{
			currAppl->currPanel->remove_pd() ;
			currAppl->clear_msg() ;
			zcommand = "" ;
		}
		else
		{
			zcommand = "SWAP" ;
			zparm    = "LIST" ;
		}
		passthru = false ;
		return ;
	}

	if ( c == KEY_ENTER && !ctlkeyPressed )
	{
		if ( wmPending )
		{
			currAppl->clear_msg() ;
			currScrn->save_panel_stack() ;
			currAppl->rempop() ;
			currAppl->addpop( "", row-currAppl->get_addpop_row()-1, col-currAppl->get_addpop_col()-3 ) ;
			currAppl->movepop() ;
			currScrn->restore_panel_stack() ;
			wmPending = false ;
			passthru  = false ;
			zcommand  = "NOP" ;
			currScrn->set_cursor( currAppl ) ;
			currAppl->display_pd( err ) ;
			currAppl->display_id() ;
			return ;
		}
		else if ( currAppl->currPanel->on_border_line( row, col ) )
		{
			issueMessage( "PSYS015" ) ;
			zcommand  = "NOP" ;
			wmPending = true  ;
			passthru  = false ;
			return ;
		}
		else if ( currAppl->currPanel->hide_msg_window( row, col ) )
		{
			zcommand  = "NOP" ;
			passthru  = false ;
			return ;
		}
		else if ( currAppl->currPanel->jump_field( row, col, t ) )
		{
			currAppl->currPanel->cmd_setvalue( t ) ;
		}
		else if ( currAppl->currPanel->on_abline( row ) )
		{
			if ( currAppl->currPanel->display_pd( err, row+2, col, msg ) )
			{
				if ( msg != "" )
				{
					issueMessage( msg ) ;
				}
				passthru = false ;
				currScrn->set_cursor( currAppl ) ;
				zcommand = "NOP" ;
				return ;
			}
			else if ( err.error() )
			{
				errorScreen( 1, "Error processing pull-down menu." ) ;
				serviceCallError( err ) ;
				currAppl->set_cursor_home() ;
				currScrn->set_cursor( currAppl ) ;
				return ;
			}
		}
		else if ( currAppl->currPanel->pd_active() )
		{
			if ( !currAppl->currPanel->cursor_on_pulldown( row, col ) )
			{
				currAppl->currPanel->remove_pd() ;
				currAppl->clear_msg() ;
				zcommand = "NOP" ;
				passthru = false ;
				return ;
			}
			t_pdc = currAppl->currPanel->retrieve_choice( err, msg ) ;
			if ( t_pdc.pdc_inact )
			{
				msg = "PSYS012T" ;
			}
			if ( msg != "" )
			{
				issueMessage( msg ) ;
				zcommand = "NOP" ;
				passthru = false ;
				return ;
			}
			else if ( t_pdc.pdc_run != "" )
			{
				currAppl->currPanel->remove_pd() ;
				currAppl->clear_msg() ;
				zcommand = t_pdc.pdc_run ;
				if ( zcommand == "ISRROUTE" )
				{
					cmdVerb = word( t_pdc.pdc_parm, 1 )    ;
					cmdParm = subword( t_pdc.pdc_parm, 2 ) ;
					if ( cmdVerb == "SELECT" )
					{
						if ( !selct.parse( err, cmdParm ) )
						{
							llog( "E", "Error in SELECT command "+ t_pdc.pdc_parm << endl ) ;
							currAppl->setmsg( "PSYS011K" ) ;
							return ;
						}
						resolvePGM()     ;
						doSelect = true  ;
						passthru = false ;
						return           ;
					}
				}
				else
				{
					zcommand += " " + t_pdc.pdc_parm ;
				}
			}
			else
			{
				currAppl->currPanel->remove_pd() ;
				currAppl->clear_msg() ;
				zcommand = "" ;
				return ;
			}
		}
	}

	if ( wmPending )
	{
		wmPending = false ;
		currAppl->clear_msg() ;
	}

	addRetrieve = true ;
	delm        = p_poolMGR->sysget( err, "ZDEL", PROFILE ) ;

	if ( t_pdc.pdc_run == "" )
	{
		zcommand = strip( currAppl->currPanel->cmd_getvalue() ) ;
	}

	if ( c == KEY_ENTER  &&
	     zcommand != ""  &&
	     currAppl->currPanel->has_command_field() &&
	     p_poolMGR->sysget( err, "ZSWAPC", PROFILE ) == zcommand.substr( 0, 1 ) &&
	     p_poolMGR->sysget( err, "ZSWAP",  PROFILE ) == "Y" )
	{
		currAppl->currPanel->field_get_row_col( currAppl->currPanel->cmdfield, rw, cl ) ;
		if ( rw == row && cl < col )
		{
			zparm    = upper( zcommand.substr( 1, col-cl-1 ) ) ;
			zcommand = "SWAP" ;
			passthru = false  ;
			currAppl->currPanel->cmd_setvalue( "" ) ;
			return ;
		}
	}

	if ( ctlkeyPressed )
	{
		pfcmd = ctlAction ;
	}

	if ( commandStack != "" )
	{
		if ( zcommand != "" )
		{
			currAppl->currPanel->cmd_setvalue( zcommand + commandStack ) ;
			commandStack = "" ;
			return ;
		}
		else
		{
			zcommand     = commandStack ;
			commandStack = ""    ;
			addRetrieve  = false ;
		}
	}

	if ( pfkeyPressed )
	{
		if ( p_poolMGR->sysget( err, "ZKLUSE", PROFILE ) == "Y" )
		{
			currAppl->save_errblock() ;
			currAppl->reload_keylist( currAppl->currPanel ) ;
			err = currAppl->get_errblock() ;
			currAppl->restore_errblock()   ;
			if ( err.error() )
			{
				serviceCallError( err ) ;
			}
			else
			{
				pfcmd = currAppl->currPanel->get_keylist( c ) ;
			}
		}
		if ( pfcmd == "" )
		{
			pfcmd = pfKeyValue( c ) ;
		}
		t = "PF" + d2ds( c - KEY_F( 0 ), 2 ) ;
		p_poolMGR->put( err, "ZPFKEY", t, SHARED, SYSTEM ) ;
		debug1( "PF Key pressed " <<t<<" value "<< pfcmd << endl ) ;
		currAppl->currPanel->set_pfpressed( t ) ;
	}
	else
	{
		p_poolMGR->put( err, "ZPFKEY", "PF00", SHARED, SYSTEM ) ;
		if ( err.error() )
		{
			llog( "C", "VPUT for PF00 failed" << endl ) ;
			listErrorBlock( err ) ;
		}
		currAppl->currPanel->set_pfpressed( "" ) ;
	}

	if ( addRetrieve )
	{
		rbsize = ds2d( p_poolMGR->sysget( err, "ZRBSIZE", PROFILE ) ) ;
		rtsize = ds2d( p_poolMGR->sysget( err, "ZRTSIZE", PROFILE ) ) ;
		if ( retrieveBuffer.capacity() != rbsize )
		{
			retrieveBuffer.rset_capacity( rbsize ) ;
		}
		if (  zcommand.size() >= rtsize &&
		     !findword( word( upper( zcommand ), 1 ), "RETRIEVE RETF RETP" ) &&
		     !findword( word( upper( pfcmd ), 1 ), "RETRIEVE RETF RETP" ) )
		{
			itt = find( retrieveBuffer.begin(), retrieveBuffer.end(), zcommand ) ;
			if ( itt != retrieveBuffer.end() )
			{
				retrieveBuffer.erase( itt ) ;
			}
			retrieveBuffer.push_front( zcommand ) ;
			retPos = 0 ;
		}
	}

	switch( c )
	{
		case KEY_NPAGE:
			zcommand = "DOWN "+ zcommand ;
			break ;
		case KEY_PPAGE:
			zcommand = "UP "+ zcommand  ;
			break ;
	}

	if ( pfcmd != "" ) { zcommand = pfcmd + " " + zcommand ; }

	if ( zcommand.compare( 0, 2, delm+delm ) == 0 )
	{
		commandStack = zcommand.substr( 1 ) ;
		zcommand     = ""                   ;
		currAppl->currPanel->cmd_setvalue( "" ) ;
		return ;
	}
	else if ( zcommand.compare( 0, 1, delm ) == 0 )
	{
		zcommand.erase( 0, 1 ) ;
		currAppl->currPanel->cmd_setvalue( zcommand ) ;
	}

	p1 = zcommand.find( delm.front() ) ;
	if ( p1 != string::npos )
	{
		commandStack = zcommand.substr( p1 ) ;
		zcommand.erase( p1 )                 ;
		currAppl->currPanel->cmd_setvalue( zcommand ) ;
	}

	cmdVerb = upper( word( zcommand, 1 ) ) ;
	cmdParm = subword( zcommand, 2 ) ;

	if ( cmdVerb == "" ) { retPos = 0 ; return ; }

	if ( cmdVerb.front() == '@' )
	{
		selct.clear() ;
		currAppl->vcopy( "ZOREXPGM", selct.pgm, MOVE ) ;
		selct.parm    = zcommand.substr( 1 ) ;
		selct.newappl = ""    ;
		selct.newpool = true  ;
		selct.passlib = false ;
		selct.suspend = true  ;
		doSelect      = true  ;
		passthru      = false ;
		currAppl->currPanel->cmd_setvalue( "" ) ;
		return ;
	}
	else if ( cmdVerb.front() == '!' )
	{
		selct.clear() ;
		selct.pgm     = cmdVerb.substr( 1 ) ;
		selct.parm    = cmdParm ;
		selct.newappl = ""      ;
		selct.newpool = true    ;
		selct.passlib = false   ;
		selct.suspend = true    ;
		doSelect      = true    ;
		passthru      = false   ;
		currAppl->currPanel->cmd_setvalue( "" ) ;
		return ;
	}

	if ( zcommand.size() > 1 && zcommand.front() == '=' && ( pfcmd == "" || pfcmd == "RETURN" ) )
	{
		zcommand.erase( 0, 1 ) ;
		jumpOption = zcommand + commandStack ;
		passthru   = true ;
		if ( !currAppl->isprimMenu() )
		{
			currAppl->jumpEntered = true ;
			p_poolMGR->put( err, "ZVERB", "RETURN", SHARED ) ;
			currAppl->currPanel->cmd_setvalue( "" ) ;
			return ;
		}
		currAppl->currPanel->cmd_setvalue( zcommand ) ;
	}

	if ( cmdVerb == "RETRIEVE" || cmdVerb == "RETF" )
	{
		if ( !currAppl->currPanel->has_command_field() ) { return ; }
		if ( datatype( cmdParm, 'W' ) )
		{
			p1 = ds2d( cmdParm ) ;
			if ( p1 > 0 && p1 <= retrieveBuffer.size() ) { retPos = p1 - 1 ; }
		}
		commandStack = ""    ;
		zcommand     = "NOP" ;
		passthru     = false ;
		if ( !retrieveBuffer.empty() )
		{
			if ( cmdVerb == "RETF" )
			{
				retPos = ( retPos < 2 ) ? retrieveBuffer.size() : retPos - 1 ;
			}
			else
			{
				if ( ++retPos > retrieveBuffer.size() ) { retPos = 1 ; }
			}
			currAppl->currPanel->cmd_setvalue( retrieveBuffer[ retPos-1 ] ) ;
			currAppl->currPanel->cursor_to_cmdfield( RC, retrieveBuffer[ retPos-1 ].size()+1 ) ;
		}
		else
		{
			currAppl->currPanel->cmd_setvalue( "" ) ;
			currAppl->currPanel->cursor_to_cmdfield( RC ) ;
		}
		currScrn->set_cursor( currAppl ) ;
		currAppl->currPanel->remove_pd() ;
		currAppl->clear_msg() ;
		return ;
	}
	retPos = 0 ;

	if ( cmdVerb == "HELP")
	{
		commandStack = "" ;
		if ( currAppl->currPanel->msgid == "" || currAppl->currPanel->showLMSG )
		{
			currAppl->currPanel->cmd_setvalue( "" ) ;
			zparm   = currAppl->get_help_member( row, col ) ;
			cmdParm = zparm ;
		}
		else
		{
			zcommand = "NOP" ;
			passthru = false ;
			currAppl->currPanel->showLMSG = true ;
			currAppl->currPanel->display_msg( err ) ;
			return ;
		}
	}

	aVerb = cmdVerb ;

	if ( resolveZCTEntry( cmdVerb, cmdParm ) )
	{
		if ( word( zctact, 1 ) == "NOP" )
		{
			p_poolMGR->put( err, "ZCTMVAR", left( aVerb, 8 ), SHARED ) ;
			issueMessage( "PSYS011" ) ;
			currAppl->currPanel->cmd_setvalue( "" ) ;
			passthru = false ;
			return ;
		}
		else if ( word( zctact, 1 ) == "SELECT" )
		{
			if ( !selct.parse( err, subword( zctact, 2 ) ) )
			{
				llog( "E", "Error in SELECT command "+ zctact << endl ) ;
				currAppl->setmsg( "PSYS011K" ) ;
				return ;
			}
			p1 = selct.parm.find( "&ZPARM" ) ;
			if ( p1 != string::npos )
			{
				selct.parm.replace( p1, 6, cmdParm ) ;
			}
			resolvePGM() ;
			if ( selct.pgm.front() == '&' )
			{
				currAppl->vcopy( substr( selct.pgm, 2 ), selct.pgm, MOVE ) ;
			}
			zcommand     = word( zctact, 1 ) ;
			doSelect     = true  ;
			selct.nested = true  ;
			passthru     = false ;
		}
		else if ( zctact == "PASSTHRU" )
		{
			passthru = true ;
			zcommand = strip( cmdVerb + " " + cmdParm ) ;
		}
		else if ( zctact == "SETVERB" )
		{
			if ( currAppl->currPanel->is_cmd_inactive( cmdVerb ) )
			{
				p_poolMGR->put( err, "ZCTMVAR", left( aVerb, 8 ), SHARED ) ;
				issueMessage( "PSYS011" ) ;
				currAppl->currPanel->cmd_setvalue( "" ) ;
				passthru = false ;
				zcommand = "NOP" ;
				return ;
			}
			else
			{
				passthru = true ;
				p_poolMGR->put( err, "ZVERB", zctverb, SHARED ) ;
				if ( err.error() )
				{
					llog( "C", "VPUT for ZVERB failed" << endl ) ;
					listErrorBlock( err ) ;
				}
				zcommand = subword( zcommand, 2 ) ;
			}
			if ( cmdVerb == "NRETRIEV" )
			{
				selct.clear() ;
				currAppl->vcopy( "ZRFLPGM", selct.pgm, MOVE ) ;
				selct.parm = "NR1 " + cmdParm ;
				doSelect   = true  ;
				passthru   = false ;
			}
		}
	}

	if ( zcommands.find( cmdVerb ) != zcommands.end() )
	{
		passthru = false   ;
		zcommand = cmdVerb ;
		zparm    = upper( cmdParm ) ;
	}

	if ( cmdVerb.front() == '>' )
	{
		zcommand.erase( 0, 1 ) ;
	}

	if ( currAppl->currPanel->pd_active() && zctverb == "END" )
	{
		currAppl->currPanel->remove_pd() ;
		currAppl->clear_msg() ;
		zcommand = ""    ;
		passthru = false ;
	}

	currAppl->currPanel->cmd_setvalue( passthru ? zcommand : "" ) ;
	debug1( "Primary command '"+ zcommand +"'  Passthru = " << passthru << endl ) ;
}


void processZCOMMAND( uint row, uint col, bool doSelect )
{
	// If the event is not being passed to the application, process ZCOMMAND or start application request

	string w1 ;
	string w2 ;
	string field_name ;

	fieldExc fxc ;

	auto it = zcommands.find( zcommand ) ;

	if ( it == zcommands.end() )
	{
		currAppl->set_cursor_home() ;
		currScrn->set_cursor( currAppl ) ;
		if ( doSelect )
		{
			updateDefaultVars() ;
			if ( selct.backgrd )
			{
				startApplicationBack( selct ) ;
			}
			else
			{
				currAppl->currPanel->remove_pd() ;
				startApplication( selct ) ;
			}
		}
		return ;
	}

	switch ( it->second )
	{
	case ZC_ACTIONS:
		displayNextPullDown( row, col ) ;
		return ;

	case ZC_DISCARD:
		currAppl->currPanel->refresh_fields( err ) ;
		break  ;

	case ZC_FIELDEXC:
		field_name = currAppl->currPanel->field_getname( row, col ) ;
		if ( field_name == "" )
		{
			issueMessage( "PSYS012K" ) ;
			return ;
		}
		fxc = currAppl->currPanel->field_getexec( field_name ) ;
		if ( fxc.fieldExc_command != "" )
		{
			executeFieldCommand( field_name, fxc, col ) ;
		}
		else
		{
			issueMessage( "PSYS012J" ) ;
		}
		return ;

	case ZC_MSGID:
		if ( zparm == "" )
		{
			w1 = p_poolMGR->get( err, currScrn->screenId, "ZMSGID" ) ;
			p_poolMGR->put( err, "ZMSGID", w1, SHARED, SYSTEM ) ;
			issueMessage( "PSYS012L" ) ;
		}
		else if ( zparm == "ON" )
		{
			p_poolMGR->put( err, currScrn->screenId, "ZSHMSGID", "Y" ) ;
		}
		else if ( zparm == "OFF" )
		{
			p_poolMGR->put( err, currScrn->screenId, "ZSHMSGID", "N" ) ;
		}
		else
		{
			issueMessage( "PSYS012M" ) ;
		}
		break  ;

	case ZC_NOP:
		return ;

	case ZC_PANELID:
		if ( zparm == "" )
		{
			w1 = p_poolMGR->get( err, currScrn->screenId, "ZSHPANID" ) ;
			zparm = ( w1 == "Y" ) ? "OFF" : "ON" ;
		}
		if ( zparm == "ON" )
		{
			p_poolMGR->put( err, currScrn->screenId, "ZSHPANID", "Y" ) ;
		}
		else if ( zparm == "OFF" )
		{
			p_poolMGR->put( err, currScrn->screenId, "ZSHPANID", "N" ) ;
		}
		else
		{
			issueMessage( "PSYS014" ) ;
		}
		currAppl->display_id() ;
		break  ;

	case ZC_REFRESH:
		currScrn->refresh_panel_stack() ;
		currScrn->OIA_refresh() ;
		redrawwin( stdscr ) ;
		break  ;

	case ZC_RETP:
		listRetrieveBuffer() ;
		return ;

	case ZC_RESIZE:
		currAppl->toggle_fscreen() ;
		break  ;

	case ZC_SCRNAME:
		w1    = word( zparm, 2 ) ;
		zparm = word( zparm, 1 ) ;
		if  ( zparm == "ON" )
		{
			p_poolMGR->put( err, "ZSCRNAM1", "ON", SHARED, SYSTEM ) ;
		}
		else if ( zparm == "OFF" )
		{
			p_poolMGR->put( err, "ZSCRNAM1", "OFF", SHARED, SYSTEM ) ;
		}
		else if ( isvalidName( zparm ) && !findword( zparm, "LIST PREV NEXT" ) )
		{
			p_poolMGR->put( err, currScrn->screenId, "ZSCRNAME", zparm ) ;
			p_poolMGR->put( err, "ZSCRNAME", zparm, SHARED ) ;
			if ( w1 == "PERM" )
			{
				p_poolMGR->put( err, currScrn->screenId, "ZSCRNAM2", w1 ) ;
			}
			else
			{
				p_poolMGR->put( err, currScrn->screenId, "ZSCRNAM2", "" ) ;
			}
		}
		else
		{
			issueMessage( "PSYS013" ) ;
		}
		currAppl->display_id() ;
		break  ;

	case ZC_SPLIT:
		if ( currAppl->msg_issued_with_cmd() )
		{
			currAppl->clear_msg() ;
		}
		selct.def( gmainpgm ) ;
		startApplication( selct, true ) ;
		return ;

	case ZC_SWAP:
		actionSwap( row, col ) ;
		return ;

	case ZC_TDOWN:
		currAppl->currPanel->field_tab_down( row, col ) ;
		currScrn->set_cursor( row, col ) ;
		return ;

	case ZC_USERID:
		if ( zparm == "" )
		{
			w1 = p_poolMGR->get( err, currScrn->screenId, "ZSHUSRID" ) ;
			zparm = ( w1 == "Y" ) ? "OFF" : "ON" ;
		}
		if ( zparm == "ON" )
		{
			p_poolMGR->put( err, currScrn->screenId, "ZSHUSRID", "Y" ) ;
		}
		else if ( zparm == "OFF" )
		{
			p_poolMGR->put( err, currScrn->screenId, "ZSHUSRID", "N" ) ;
		}
		else
		{
			issueMessage( "PSYS012F" ) ;
		}
		currAppl->display_id() ;
		break  ;

	case ZC_DOT_ABEND:
		currAppl->set_forced_abend() ;
		ResumeApplicationAndWait()   ;
		if ( currAppl->abnormalEnd )
		{
			terminateApplication() ;
			if ( pLScreen::screensTotal == 0 ) { return ; }
		}
		return ;

	case ZC_DOT_AUTO:
		currAppl->set_cursor( row, col ) ;
		autoUpdate() ;
		return ;

	case ZC_DOT_HIDE:
		if ( zparm == "NULLS" )
		{
			p_poolMGR->sysput( err, "ZNULLS", "NO", SHARED ) ;
			field::field_nulls = false ;
			currAppl->currPanel->redraw_fields( err ) ;
		}
		else
		{
			issueMessage( "PSYS012R" ) ;
		}
		break  ;

	case ZC_DOT_INFO:
		currAppl->info() ;
		break  ;

	case ZC_DOT_LOAD:
		for ( auto ita = apps.begin() ; ita != apps.end() ; ita++ )
		{
			if ( !ita->second.errors && !ita->second.dlopened )
			{
				loadDynamicClass( ita->first ) ;
			}
		}
		break  ;

	case ZC_DOT_RELOAD:
		reloadDynamicClasses( zparm ) ;
		break  ;

	case ZC_DOT_SCALE:
		if ( zparm == "" ) { zparm = "ON" ; }
		if ( findword( zparm, "ON OFF" ) )
		{
			p_poolMGR->sysput( err, "ZSCALE", zparm, SHARED ) ;
		}
		break  ;

	case ZC_DOT_SHELL:
		def_prog_mode()   ;
		endwin()          ;
		system( p_poolMGR->sysget( err, "ZSHELL", SHARED ).c_str() ) ;
		reset_prog_mode() ;
		refresh()         ;
		break  ;

	case ZC_DOT_SHOW:
		if ( zparm == "NULLS" )
		{
			p_poolMGR->sysput( err, "ZNULLS", "YES", SHARED ) ;
			field::field_nulls = true ;
			currAppl->currPanel->redraw_fields( err ) ;
		}
		else
		{
			issueMessage( "PSYS013A" ) ;
		}
		break  ;

	case ZC_DOT_STATS:
		p_poolMGR->statistics() ;
		p_tableMGR->statistics() ;
		break  ;

	case ZC_DOT_SNAP:
		p_poolMGR->snap() ;
		break  ;

	case ZC_DOT_TASKS:
		listBackTasks() ;
		break  ;

	case ZC_DOT_TEST:
		currAppl->setTestMode() ;
		llog( "W", "Application is now running in test mode" << endl ) ;
		break  ;
	}

	currAppl->set_cursor_home() ;
	currScrn->set_cursor( currAppl ) ;
}


bool resolveZCTEntry( string& cmdVerb, string& cmdParm )
{
	// Search for command tables in ZTLIB.
	// System commands should be in ZUPROF but user/site command tables might not be.

	// Do not try to load the application command table as this will have been loaded
	// during SELECT processing if it exists.  This is always the first in the list of command tables.

	// ALIAS parameters in the command table take precedence over command line parameters.

	int i ;

	size_t j ;

	bool found = false ;

	string ztlib   ;
	string cmdtabl ;

	vector<string>cmdtabls ;
	set<string>processed   ;

	err.clear() ;

	zctverb  = "" ;
	zcttrunc = "" ;
	zctact   = "" ;
	zctdesc  = "" ;

	cmdtabl = ( currAppl->get_applid() == "ISP" ) ? "N/A" : currAppl->get_applid() ;
	cmdtabls.push_back( cmdtabl ) ;
	processed.insert( cmdtabl ) ;

	for ( i = 1 ; i < 4 ; i++ )
	{
		cmdtabl = p_poolMGR->sysget( err, "ZUCMDT" + d2ds( i ), PROFILE ) ;
		if ( cmdtabl != "" && processed.count( cmdtabl ) == 0 )
		{
			cmdtabls.push_back( cmdtabl ) ;
		}
		processed.insert( cmdtabl ) ;
	}

	if ( p_poolMGR->sysget( err, "ZSCMDTF", PROFILE ) != "Y" )
	{
		  cmdtabls.push_back( "ISP" ) ;
		  processed.insert( "ISP" ) ;
	}

	for ( i = 1 ; i < 4 ; i++ )
	{
		cmdtabl = p_poolMGR->sysget( err, "ZSCMDT" + d2ds( i ), PROFILE ) ;
		if ( cmdtabl != "" && processed.count( cmdtabl ) == 0 )
		{
			cmdtabls.push_back( cmdtabl ) ;
		}
		processed.insert( cmdtabl ) ;
	}

	if ( processed.count( "ISP" ) == 0 )
	{
		  cmdtabls.push_back( "ISP" ) ;
	}

	ztlib = p_poolMGR->sysget( err, "ZTLIB", PROFILE ) ;

	for ( i = 0 ; i < 256 ; i++ )
	{
		for ( j = 0 ; j < cmdtabls.size() ; j++ )
		{
			err.clear() ;
			p_tableMGR->cmdsearch( err, funcPOOL, cmdtabls[ j ], cmdVerb, ztlib, ( j > 0 ) ) ;
			if ( err.error() )
			{
				llog( "E", "Error received searching for command "+ cmdVerb << endl ) ;
				llog( "E", "Table name : "+ cmdtabls[ j ] << endl ) ;
				llog( "E", "Path list  : "+ ztlib << endl ) ;
				listErrorBlock( err ) ;
				continue ;
			}
			if ( !err.RC0() || zctact == "" ) { continue ; }
			if ( zctact.front() == '&' )
			{
				currAppl->vcopy( substr( zctact, 2 ), zctact, MOVE ) ;
				if ( zctact == "" ) { found = false ; continue ; }
			}
			found = true ;
			break ;
		}
		if ( err.getRC() > 0 || word( zctact, 1 ) != "ALIAS" ) { break ; }
		cmdVerb = word( zctact, 2 )    ;
		if ( subword( zctact, 3 ) != "" )
		{
			cmdParm = subword( zctact, 3 ) ;
		}
		zcommand = cmdVerb + " " + cmdParm ;
	}

	if ( i > 255 )
	{
		llog( "E", "ALIAS dept cannot be greater than 256.  Terminating search" << endl ) ;
		found = false ;
	}

	return found ;
}


void processPGMSelect()
{
	// Called when an application program has invoked the SELECT service (also VIEW, EDIT, BROWSE)

	selct = currAppl->get_select_cmd() ;

	resolvePGM() ;

	if ( apps.find( selct.pgm ) != apps.end() )
	{
		updateDefaultVars() ;
		if ( selct.backgrd )
		{
			startApplicationBack( selct, true ) ;
			ResumeApplicationAndWait() ;
		}
		else
		{
			startApplication( selct ) ;
		}
	}
	else
	{
		currAppl->RC      = 20  ;
		currAppl->ZRC     = 20  ;
		currAppl->ZRSN    = 998 ;
		currAppl->ZRESULT = "Not Found" ;
		if ( !currAppl->errorsReturn() )
		{
			currAppl->abnormalEnd = true ;
		}
		ResumeApplicationAndWait() ;
		while ( currAppl->terminateAppl )
		{
			terminateApplication() ;
			if ( pLScreen::screensTotal == 0 ) { return ; }
		}
	}
}


void resolvePGM()
{
	// Add application to run for SELECT PANEL() and SELECT CMD() services

	if ( selct.rexpgm )
	{
		currAppl->vcopy( "ZOREXPGM", selct.pgm, MOVE ) ;
	}
	else if ( selct.panpgm )
	{
		currAppl->vcopy( "ZPANLPGM", selct.pgm, MOVE ) ;
	}
}


void startApplication( selobj& sSelect, bool nScreen )
{
	// Start an application using the passed SELECT object, on a new logical screen if specified.

	// If the program is ISPSTRT, start the application in the PARM field on a new logical screen
	// or start GMAINPGM.  Force NEWPOOL option regardless of what is coded in the command.
	// PARM can be a command table entry, a PGM()/CMD()/PANEL() statement or an option for GMAINPGM.

	int elapsed ;
	int spool   ;

	size_t p1   ;

	string opt   ;
	string rest  ;
	string sname ;
	string applid ;

	bool setMessage ;

	err.clear() ;

	pApplication* oldAppl = currAppl ;

	boost::thread* pThread ;

	if ( sSelect.pgm == "ISPSTRT" )
	{
		currAppl->clear_msg() ;
		nScreen = true ;
		opt     = upper( sSelect.parm ) ;
		rest    = ""   ;
		if ( isvalidName( opt ) && resolveZCTEntry( opt, rest ) && word( zctact, 1 ) == "SELECT" )
		{
			if ( !sSelect.parse( err, subword( zctact, 2 ) ) )
			{
				errorScreen( 1, "Error in ZCTACT command "+ zctact ) ;
				return ;
			}
			resolvePGM() ;
			p1 = sSelect.parm.find( "&ZPARM" ) ;
			if ( p1 != string::npos )
			{
				sSelect.parm.replace( p1, 6, rest ) ;
			}
		}
		else if ( !sSelect.parse( err, sSelect.parm ) )
		{
			sSelect.def( gmainpgm ) ;
			sSelect.parm = opt ;
		}
		if ( sSelect.pgm.front() == '&' )
		{
			currAppl->vcopy( substr( sSelect.pgm, 2 ), sSelect.pgm, MOVE ) ;
		}
		sSelect.newpool = true ;
	}

	if ( apps.find( sSelect.pgm ) == apps.end() )
	{
		sSelect.errors = true ;
		if ( sSelect.quiet ) { return ; }
		errorScreen( 1, "Application '"+ sSelect.pgm +"' not found" ) ;
		return ;
	}

	if ( !apps[ sSelect.pgm ].dlopened )
	{
		if ( !loadDynamicClass( sSelect.pgm ) )
		{
			errorScreen( 1, "Errors loading application "+ sSelect.pgm ) ;
			return ;
		}
	}

	if ( nScreen && !createLogicalScreen() ) { return ; }

	currAppl->store_scrname() ;
	spool      = currAppl->shrdPool   ;
	setMessage = currAppl->setMessage ;
	sname      = p_poolMGR->get( err, "ZSCRNAME", SHARED ) ;
	if ( setMessage )
	{
		currAppl->setMessage = false ;
	}

	llog( "I", "Starting application "+ sSelect.pgm +" with parameters '"+ sSelect.parm +"'" << endl ) ;
	currAppl = ((pApplication*(*)())( apps[ sSelect.pgm ].maker_ep))() ;

	currScrn->application_add( currAppl ) ;

	currAppl->init_phase1( sSelect, ++maxtaskID, lspfCallbackHandler ) ;
	currAppl->set_output_done( oldAppl->line_output_done() ) ;

	apps[ sSelect.pgm ].refCount++ ;

	applid = ( sSelect.newappl == "" ) ? oldAppl->get_applid() : sSelect.newappl ;
	err.settask( currAppl->taskid() ) ;

	if ( sSelect.newpool )
	{
		if ( currScrn->application_stack_size() > 1 && selct.scrname == "" )
		{
			selct.scrname = sname ;
		}
		spool = p_poolMGR->createSharedPool() ;
	}

	p_poolMGR->createProfilePool( err, applid ) ;
	p_poolMGR->connect( currAppl->taskid(), applid, spool ) ;
	if ( err.RC4() ) { createpfKeyDefaults() ; }

	createSharedPoolVars( applid ) ;

	currAppl->shrdPool = spool ;
	currAppl->init_phase2() ;

	if ( !sSelect.suspend )
	{
		currAppl->set_addpop_row( oldAppl->get_addpop_row() ) ;
		currAppl->set_addpop_col( oldAppl->get_addpop_col() ) ;
		currAppl->set_addpop_act( oldAppl->get_addpop_act() ) ;
	}

	if ( !nScreen && ( sSelect.passlib || sSelect.newappl == "" ) )
	{
		currAppl->set_zlibd( oldAppl->get_zlibd() ) ;
	}

	if ( setMessage )
	{
		currAppl->set_msg1( oldAppl->getmsg1(), oldAppl->getmsgid1() ) ;
	}
	else if ( !nScreen )
	{
		oldAppl->clear_msg() ;
	}

	currAppl->loadCommandTable() ;
	pThread = new boost::thread( &pApplication::run, currAppl ) ;

	currAppl->pThread = pThread ;

	if ( selct.scrname != "" )
	{
		p_poolMGR->put( err, "ZSCRNAME", selct.scrname, SHARED ) ;
	}

	llog( "I", "Waiting for new application to complete startup.  ID=" << pThread->get_id() << endl ) ;
	elapsed = 0 ;
	while ( currAppl->busyAppl )
	{
		if ( not currAppl->noTimeOut ) { elapsed++ ; }
		boost::this_thread::sleep_for( boost::chrono::milliseconds( ZWAIT ) ) ;
		if ( elapsed > gmaxwait  ) { currAppl->set_timeout_abend() ; }
	}

	if ( currAppl->do_refresh_lscreen() )
	{
		if ( linePosn != -1 )
		{
			lineOutput_end() ;
			linePosn = -1 ;
		}
		currScrn->refresh_panel_stack() ;
	}

	if ( currAppl->abnormalEnd )
	{
		abnormalTermMessage()  ;
		terminateApplication() ;
		if ( pLScreen::screensTotal == 0 ) { return ; }
	}
	else
	{
		llog( "I", "New thread and application started and initialised. ID=" << pThread->get_id() << endl ) ;
		if ( currAppl->SEL )
		{
			processPGMSelect() ;
		}
		else if ( currAppl->line_output_pending() )
		{
			lineOutput() ;
		}
	}

	while ( currAppl->terminateAppl )
	{
		terminateApplication() ;
		if ( pLScreen::screensTotal == 0 ) { return ; }
		if ( currAppl->SEL && !currAppl->terminateAppl )
		{
			processPGMSelect() ;
		}
	}
	currScrn->set_cursor( currAppl ) ;
}


void startApplicationBack( selobj sSelect, bool pgmselect )
{
	// Start a background application using the passed SELECT object

	int spool ;

	string applid ;

	errblock err1 ;

	pApplication* oldAppl = currAppl ;
	pApplication* Appl ;

	boost::thread* pThread ;

	if ( apps.find( sSelect.pgm ) == apps.end() )
	{
		llog( "E", "Application '"+ sSelect.pgm +"' not found" <<endl ) ;
		return ;
	}

	if ( !apps[ sSelect.pgm ].dlopened )
	{
		if ( !loadDynamicClass( sSelect.pgm ) )
		{
			llog( "E", "Errors loading "+ sSelect.pgm <<endl ) ;
			return ;
		}
	}

	llog( "I", "Starting background application "+ sSelect.pgm +" with parameters '"+ sSelect.parm +"'" <<endl ) ;
	Appl = ((pApplication*(*)())( apps[ sSelect.pgm ].maker_ep))() ;

	Appl->init_phase1( sSelect, ++maxtaskID, lspfCallbackHandler ) ;

	apps[ sSelect.pgm ].refCount++ ;

	mtx.lock() ;
	pApplicationBackground.push_back( Appl ) ;
	mtx.unlock() ;

	applid = ( sSelect.newappl == "" ) ? oldAppl->get_applid() : sSelect.newappl ;
	err1.settask( Appl->taskid() ) ;

	if ( sSelect.newpool )
	{
		spool = p_poolMGR->createSharedPool() ;
	}
	else
	{
		spool  = oldAppl->shrdPool ;
	}

	if ( pgmselect )
	{
		oldAppl->ZTASKID = Appl->taskid() ;
	}

	p_poolMGR->createProfilePool( err1, applid ) ;
	p_poolMGR->connect( Appl->taskid(), applid, spool ) ;

	createSharedPoolVars( applid ) ;

	Appl->shrdPool = spool ;
	Appl->init_phase2() ;

	if ( sSelect.passlib || sSelect.newappl == "" )
	{
		Appl->set_zlibd( oldAppl->get_zlibd() ) ;
	}

	pThread = new boost::thread( &pApplication::run, Appl ) ;

	Appl->pThread = pThread ;

	llog( "I", "New background thread and application started and initialised. ID=" << pThread->get_id() << endl ) ;
}


void processBackgroundTasks()
{
	// This routine runs every 100ms in a separate thread to check if there are any
	// background tasks waiting for action:
	//    cleanup application if ended
	//    start application if SELECT() done in the background program

	backStatus = BACK_RUNNING ;

	boost::thread* pThread ;

	while ( lspfStatus == LSPF_RUNNING )
	{
		boost::this_thread::sleep_for( boost::chrono::milliseconds( 100 ) ) ;
		mtx.lock() ;
		for ( auto it = pApplicationBackground.begin() ; it != pApplicationBackground.end() ; it++ )
		{
			while ( (*it)->terminateAppl )
			{
				llog( "I", "Removing background application instance of "+
					(*it)->get_appname() << " taskid: " << (*it)->taskid() << endl ) ;
				p_poolMGR->disconnect( (*it)->taskid() ) ;
				pThread = (*it)->pThread ;
				apps[ (*it)->get_appname() ].refCount-- ;
				((void (*)(pApplication*))(apps[ (*it)->get_appname() ].destroyer_ep))( (*it) ) ;
				delete pThread ;
				it = pApplicationBackground.erase( it ) ;
				if ( it == pApplicationBackground.end() ) { break ; }
			}
			if ( it == pApplicationBackground.end() ) { break ; }
		}
		mtx.unlock() ;

		mtx.lock() ;
		vector<pApplication*> temp = pApplicationBackground ;

		for ( auto it = temp.begin() ; it != temp.end() ; it++ )
		{
			if ( (*it)->SEL && !currAppl->terminateAppl )
			{
				startApplicationBack( (*it)->get_select_cmd() ) ;
				(*it)->busyAppl = true  ;
				cond_appl.notify_all()  ;
			}
		}
		mtx.unlock() ;

		if ( backStatus == BACK_STOPPING )
		{
			for ( auto it = pApplicationBackground.begin() ; it != pApplicationBackground.end() ; it++ )
			{
				llog( "I", "lspf shutting down.  Removing background application instance of "+
					(*it)->get_appname() << " taskid: " << (*it)->taskid() << endl ) ;
				p_poolMGR->disconnect( (*it)->taskid() ) ;
				pThread = (*it)->pThread ;
				apps[ (*it)->get_appname() ].refCount-- ;
				((void (*)(pApplication*))(apps[ (*it)->get_appname() ].destroyer_ep))( (*it) ) ;
				delete pThread ;
			}
		}
	}

	llog( "I", "Background job monitor task stopped" << endl ) ;
	backStatus = BACK_STOPPED ;
}


void terminateApplication()
{
	int tRC  ;
	int tRSN ;

	uint row ;
	uint col ;

	string zappname ;
	string tRESULT  ;
	string tMSGID1  ;
	string fname    ;
	string delm     ;

	bool refList       ;
	bool nretError     ;
	bool propagateEnd  ;
	bool abnormalEnd   ;
	bool jumpEntered   ;
	bool setCursorHome ;
	bool setMessage    ;
	bool lineOutput    ;
	bool nested        ;

	slmsg tMSG1 ;

	err.clear() ;

	pApplication* prvAppl ;

	boost::thread* pThread ;

	llog( "I", "Application terminating "+ currAppl->get_appname() +" ID: "<< currAppl->taskid() << endl ) ;

	zappname = currAppl->get_appname() ;

	tRC     = currAppl->ZRC  ;
	tRSN    = currAppl->ZRSN ;
	tRESULT = currAppl->ZRESULT ;
	abnormalEnd = currAppl->abnormalEnd ;

	refList = ( currAppl->reffield == "#REFLIST" ) ;

	setMessage = currAppl->setMessage ;
	if ( setMessage ) { tMSGID1 = currAppl->getmsgid1() ; tMSG1 = currAppl->getmsg1() ; }

	jumpEntered  = currAppl->jumpEntered ;
	propagateEnd = currAppl->propagateEnd && ( currScrn->application_stack_size() > 1 ) ;
	lineOutput   = currAppl->line_output_done() ;
	nested       = currAppl->get_nested()       ;

	pThread = currAppl->pThread ;

	if ( currAppl->abnormalEnd )
	{
		while ( currAppl->cleanupRunning() )
		{
			boost::this_thread::sleep_for( boost::chrono::milliseconds( ZWAIT ) ) ;
		}
		pThread->detach() ;
	}

	p_poolMGR->disconnect( currAppl->taskid() ) ;

	currScrn->application_remove_current() ;
	if ( !currScrn->application_stack_empty() && currAppl->newappl == "" )
	{
		prvAppl = currScrn->application_get_current() ;
		prvAppl->set_zlibd( currAppl->get_zlibd() ) ;
	}

	if ( currAppl->abnormalTimeout )
	{
		llog( "I", "Moving application instance of "+ zappname +" to timeout queue" << endl ) ;
		pApplicationTimeout.push_back( currAppl ) ;
	}
	else
	{
		llog( "I", "Removing application instance of "+ zappname << endl ) ;
		apps[ zappname ].refCount-- ;
		((void (*)(pApplication*))(apps[ zappname ].destroyer_ep))( currAppl ) ;
		delete pThread ;
	}

	if ( currScrn->application_stack_empty() )
	{
		p_poolMGR->destroyPool( currScrn->screenId ) ;
		if ( pLScreen::screensTotal == 1 )
		{
			delete currScrn ;
			return ;
		}
		deleteLogicalScreen() ;
	}

	currAppl = currScrn->application_get_current() ;
	err.settask( currAppl->taskid() ) ;

	currAppl->display_pd( err ) ;

	p_poolMGR->put( err, "ZPANELID", currAppl->get_panelid(), SHARED, SYSTEM ) ;

	if ( apps[ zappname ].refCount == 0 && apps[ zappname ].relPending )
	{
		apps[ zappname ].relPending = false ;
		llog( "I", "Reloading module "+ zappname +" (pending reload status)" << endl ) ;
		if ( loadDynamicClass( zappname ) )
		{
			llog( "I", "Loaded "+ zappname +" (module "+ apps[ zappname ].module +") from "+ apps[ zappname ].file << endl ) ;
		}
		else
		{
			llog( "W", "Errors occured loading "+ zappname +"  Module removed" << endl ) ;
		}
	}

	nretError = false ;
	if ( refList )
	{
		if ( currAppl->nretriev_on() )
		{
			fname = currAppl->get_nretfield() ;
			if ( fname != "" )
			{
				if ( currAppl->currPanel->field_valid( fname ) )
				{
					currAppl->reffield = fname ;
					if ( p_poolMGR->sysget( err, "ZRFMOD", PROFILE ) == "BEX" )
					{
						delm = p_poolMGR->sysget( err, "ZDEL", PROFILE ) ;
						commandStack = delm + delm ;
					}
				}
				else
				{
					llog( "E", "Invalid field "+ fname +" in .NRET panel statement" << endl ) ;
					issueMessage( "PSYS011Z" ) ;
					nretError = true ;
				}
			}
			else
			{
				issueMessage( "PSYS011Y" ) ;
				nretError = true ;
			}
		}
		else
		{
			issueMessage( "PSYS011X" ) ;
			nretError = true ;
		}
	}

	setCursorHome = true ;
	if ( currAppl->reffield != "" && !nretError )
	{
		if ( tRC == 0 )
		{
			if ( currAppl->currPanel->field_get_row_col( currAppl->reffield, row, col ) )
			{
				currAppl->currPanel->field_setvalue( currAppl->reffield, tRESULT ) ;
				currAppl->currPanel->cursor_eof( row, col ) ;
				currAppl->currPanel->set_cursor( row, col ) ;
				if ( refList )
				{
					issueMessage( "PSYS011W" ) ;
				}
				setCursorHome = false ;
			}
			else
			{
				llog( "E", "Invalid field "+ currAppl->reffield +" in .NRET panel statement" << endl )   ;
				issueMessage( "PSYS011Z" ) ;
			}
		}
		else if ( tRC == 8 )
		{
			beep() ;
			setCursorHome = false ;
		}
		currAppl->reffield = "" ;
	}

	if ( currAppl->isprimMenu() )
	{
		propagateEnd = false ;
		if ( jumpEntered ) { commandStack = jumpOption ; }
	}

	if ( currAppl->SEL )
	{
		if ( abnormalEnd )
		{
			currAppl->RC  = 20 ;
			currAppl->ZRC = 20 ;
			if ( tRC == 20 && tRSN > 900 )
			{
				currAppl->ZRSN    = tRSN    ;
				currAppl->ZRESULT = tRESULT ;
			}
			else
			{
				currAppl->ZRSN    = 999 ;
				currAppl->ZRESULT = "Abended" ;
			}
			if ( !currAppl->errorsReturn() )
			{
				currAppl->abnormalEnd = true ;
			}
		}
		else
		{
			if ( propagateEnd )
			{
				currAppl->jumpEntered = jumpEntered ;
				currAppl->RC = 4 ;
			}
			else
			{
				currAppl->RC = 0 ;
			}
			currAppl->ZRC     = tRC     ;
			currAppl->ZRSN    = tRSN    ;
			currAppl->ZRESULT = tRESULT ;
		}
		if ( setMessage )
		{
			currAppl->set_msg1( tMSG1, tMSGID1 ) ;
		}
		currAppl->set_output_done( lineOutput ) ;
		ResumeApplicationAndWait() ;
		while ( currAppl->terminateAppl )
		{
			terminateApplication() ;
			if ( pLScreen::screensTotal == 0 ) { return ; }
		}
	}
	else
	{
		if ( propagateEnd && ( not nested || jumpEntered ) )
		{
			p_poolMGR->put( err, "ZVERB", "RETURN", SHARED ) ;
			currAppl->jumpEntered = jumpEntered ;
			ResumeApplicationAndWait() ;
			while ( currAppl->terminateAppl )
			{
				terminateApplication() ;
				if ( pLScreen::screensTotal == 0 ) { return ; }
			}
		}
		if ( setMessage )
		{
			currAppl->set_msg1( tMSG1, tMSGID1, true ) ;
		}
		if ( setCursorHome )
		{
			currAppl->set_cursor_home() ;
		}
		if ( linePosn != -1 )
		{
			lineOutput_end() ;
			currScrn->refresh_panel_stack() ;
			update_panels() ;
			doupdate() ;
			linePosn = -1 ;
		}
	}

	llog( "I", "Application terminatation of "+ zappname +" completed.  Current application is "+ currAppl->get_appname() << endl ) ;
	currAppl->restore_Zvars( currScrn->screenId ) ;
	currAppl->display_id() ;
	currScrn->set_cursor( currAppl ) ;
}


bool createLogicalScreen()
{
	err.clear() ;

	if ( !currAppl->ControlSplitEnable )
	{
		p_poolMGR->put( err, "ZCTMVAR", left( zcommand, 8 ), SHARED ) ;
		issueMessage( "PSYS011" ) ;
		return false ;
	}

	if ( pLScreen::screensTotal == ZMAXSCRN )
	{
		issueMessage( "PSYS011D" ) ;
		return false ;
	}

	currScrn->save_panel_stack()  ;
	updateDefaultVars()           ;
	altScreen = priScreen         ;
	priScreen = screenList.size() ;

	screenList.push_back( new pLScreen( currScrn->screenId, ZMAXSCRN ) ) ;
	currScrn->OIA_startTime() ;

	lScreenDefaultSettings() ;
	return true ;
}


void deleteLogicalScreen()
{
	// Delete a logical screen and set the active screen to the one that opened it (or its predecessor if
	// that too has been closed)

	int openedBy = currScrn->get_openedBy() ;

	delete currScrn ;

	screenList.erase( screenList.begin() + priScreen ) ;

	if ( altScreen > priScreen ) { altScreen-- ; }

	auto it   = screenList.begin() ;
	priScreen = (*it)->get_priScreen( openedBy ) ;

	if ( priScreen == altScreen )
	{
		altScreen = ( priScreen == 0 ) ? pLScreen::screensTotal - 1 : 0 ;

	}

	currScrn = screenList[ priScreen ] ;
	currScrn->restore_panel_stack()    ;
	currScrn->OIA_startTime() ;
}


void ResumeApplicationAndWait()
{
	int elapsed ;

	if ( currAppl->applicationEnded ) { return ; }

	elapsed = 0               ;
	currAppl->busyAppl = true ;
	cond_appl.notify_all()    ;
	while ( currAppl->busyAppl )
	{
		if ( not currAppl->noTimeOut ) { elapsed++ ; }
		boost::this_thread::sleep_for( boost::chrono::milliseconds( ZWAIT ) ) ;
		if ( elapsed > gmaxwait  ) { currAppl->set_timeout_abend() ; }
	}

	if ( currAppl->reloadCUATables ) { loadCUATables() ; }
	if ( currAppl->do_refresh_lscreen() )
	{
		if ( linePosn != -1 )
		{
			lineOutput_end() ;
			linePosn = -1 ;
		}
		currScrn->refresh_panel_stack() ;
	}

	if ( currAppl->abnormalEnd )
	{
		abnormalTermMessage() ;
	}
	else if ( currAppl->SEL )
	{
		processPGMSelect() ;
	}
	else if ( currAppl->line_output_pending() )
	{
		lineOutput() ;
	}
}


void loadCUATables()
{
	// Set according to the ZC variables in ISPS profile

	setColourPair( "AB" )   ;
	setColourPair( "ABSL" ) ;
	setColourPair( "ABU" )  ;

	setColourPair( "AMT" )  ;
	setColourPair( "AWF" )  ;

	setColourPair( "CT"  )  ;
	setColourPair( "CEF" )  ;
	setColourPair( "CH"  )  ;

	setColourPair( "DT" )   ;
	setColourPair( "ET" )   ;
	setColourPair( "EE" )   ;
	setColourPair( "FP" )   ;
	setColourPair( "FK")    ;

	setColourPair( "IMT" )  ;
	setColourPair( "LEF" )  ;
	setColourPair( "LID" )  ;
	setColourPair( "LI" )   ;

	setColourPair( "NEF" )  ;
	setColourPair( "NT" )   ;

	setColourPair( "PI" )   ;
	setColourPair( "PIN" )  ;
	setColourPair( "PT" )   ;

	setColourPair( "PS")    ;
	setColourPair( "PAC" )  ;
	setColourPair( "PUC" )  ;

	setColourPair( "RP" )   ;

	setColourPair( "SI" )   ;
	setColourPair( "SAC" )  ;
	setColourPair( "SUC")   ;

	setColourPair( "VOI" )  ;
	setColourPair( "WMT" )  ;
	setColourPair( "WT" )   ;
	setColourPair( "WASL" ) ;

	setGlobalColours() ;
}


void setColourPair( const string& name )
{
	string t ;

	err.clear() ;

	pair<map<attType, unsigned int>::iterator, bool> result ;

	result = cuaAttr.insert( pair<attType, unsigned int>( cuaAttrName[ name ], WHITE ) ) ;

	map<attType, unsigned int>::iterator it = result.first ;

	t = p_poolMGR->sysget( err, "ZC"+ name, PROFILE ) ;
	if ( !err.RC0() )
	{
		llog( "E", "Variable ZC"+ name +" not found in ISPS profile" << endl ) ;
		llog( "C", "Rerun setup program to re-initialise ISPS profile" << endl ) ;
		listErrorBlock( err ) ;
		abortStartup() ;
	}

	if ( t.size() != 3 )
	{
		llog( "E", "Variable ZC"+ name +" invalid value of "+ t +".  Must be length of three "<< endl ) ;
		llog( "C", "Rerun setup program to re-initialise ISPS profile" << endl ) ;
		listErrorBlock( err ) ;
		abortStartup() ;
	}

	switch  ( t[ 0 ] )
	{
		case 'R': it->second = RED     ; break ;
		case 'G': it->second = GREEN   ; break ;
		case 'Y': it->second = YELLOW  ; break ;
		case 'B': it->second = BLUE    ; break ;
		case 'M': it->second = MAGENTA ; break ;
		case 'T': it->second = TURQ    ; break ;
		case 'W': it->second = WHITE   ; break ;

		default :  llog( "E", "Variable ZC"+ name +" has invalid value[0] "+ t << endl ) ;
			   llog( "C", "Rerun setup program to re-initialise ISPS profile" << endl ) ;
			   abortStartup() ;
	}

	switch  ( t[ 1 ] )
	{
		case 'L':  it->second = it->second | A_NORMAL ; break ;
		case 'H':  it->second = it->second | A_BOLD   ; break ;

		default :  llog( "E", "Variable ZC"+ name +" has invalid value[1] "+ t << endl ) ;
			   llog( "C", "Rerun setup program to re-initialise ISPS profile" << endl ) ;
			   abortStartup() ;
	}

	switch  ( t[ 2 ] )
	{
		case 'N':  break ;
		case 'B':  it->second = it->second | A_BLINK     ; break ;
		case 'R':  it->second = it->second | A_REVERSE   ; break ;
		case 'U':  it->second = it->second | A_UNDERLINE ; break ;

		default :  llog( "E", "Variable ZC"+ name +" has invalid value[2] "+ t << endl ) ;
			   llog( "C", "Rerun setup program to re-initialise ISPS profile" << endl ) ;
			   abortStartup() ;
	}
}


void setGlobalColours()
{
	string t ;

	map<char, unsigned int> gcolours = { { 'R', COLOR_RED,     } ,
					     { 'G', COLOR_GREEN,   } ,
					     { 'Y', COLOR_YELLOW,  } ,
					     { 'B', COLOR_BLUE,    } ,
					     { 'M', COLOR_MAGENTA, } ,
					     { 'T', COLOR_CYAN,    } ,
					     { 'W', COLOR_WHITE    } } ;

	err.clear() ;

	t = p_poolMGR->sysget( err, "ZGCLR", PROFILE ) ;
	init_pair( 1, gcolours[ t.front() ], COLOR_BLACK ) ;

	t = p_poolMGR->sysget( err, "ZGCLG", PROFILE ) ;
	init_pair( 2, gcolours[ t.front() ], COLOR_BLACK ) ;

	t = p_poolMGR->sysget( err, "ZGCLY", PROFILE ) ;
	init_pair( 3, gcolours[ t.front() ], COLOR_BLACK ) ;

	t = p_poolMGR->sysget( err, "ZGCLB", PROFILE ) ;
	init_pair( 4, gcolours[ t.front() ], COLOR_BLACK ) ;

	t = p_poolMGR->sysget( err, "ZGCLM", PROFILE ) ;
	init_pair( 5, gcolours[ t.front() ], COLOR_BLACK ) ;

	t = p_poolMGR->sysget( err, "ZGCLT", PROFILE ) ;
	init_pair( 6, gcolours[ t.front() ], COLOR_BLACK ) ;

	t = p_poolMGR->sysget( err, "ZGCLW", PROFILE ) ;
	init_pair( 7, gcolours[ t.front() ], COLOR_BLACK ) ;
}


void loadDefaultPools()
{
	// Default vars go in @DEFPROF (RO) for PROFILE and @DEFSHAR (UP) for SHARE
	// These have the SYSTEM attibute set on the variable

	string log    ;
	string ztlib  ;
	string zuprof ;
	string home   ;
	string shell  ;
	string logname ;

	err.clear() ;

	struct utsname buf ;

	if ( uname( &buf ) != 0 )
	{
		llog( "C", "System call uname has returned an error"<< endl ) ;
		abortStartup() ;
	}

	home = getEnvironmentVariable( "HOME" ) ;
	if ( home == "" )
	{
		llog( "C", "HOME variable is required and must be set"<< endl ) ;
		abortStartup() ;
	}
	zuprof = home + ZUPROF ;

	logname = getEnvironmentVariable( "LOGNAME" ) ;
	if ( logname == "" )
	{
		llog( "C", "LOGNAME variable is required and must be set"<< endl ) ;
		abortStartup() ;
	}

	shell = getEnvironmentVariable( "SHELL" ) ;

	p_poolMGR->setProfilePath( err, zuprof ) ;

	p_poolMGR->sysput( err, "ZSCREEND", d2ds( pLScreen::maxrow ), SHARED ) ;
	p_poolMGR->sysput( err, "ZSCRMAXD", d2ds( pLScreen::maxrow ), SHARED ) ;
	p_poolMGR->sysput( err, "ZSCREENW", d2ds( pLScreen::maxcol ), SHARED ) ;
	p_poolMGR->sysput( err, "ZSCRMAXW", d2ds( pLScreen::maxcol ), SHARED ) ;
	p_poolMGR->sysput( err, "ZUSER", logname, SHARED ) ;
	p_poolMGR->sysput( err, "ZHOME", home, SHARED )    ;
	p_poolMGR->sysput( err, "ZSHELL", shell, SHARED )  ;

	p_poolMGR->createProfilePool( err, "ISPS" ) ;
	if ( !err.RC0() )
	{
		llog( "C", "Loading of system profile ISPSPROF failed.  RC="<< err.getRC() << endl ) ;
		llog( "C", "Aborting startup.  Check profile pool path" << endl ) ;
		listErrorBlock( err ) ;
		abortStartup() ;
	}
	llog( "I", "Loaded system profile ISPSPROF" << endl ) ;

	log = p_poolMGR->sysget( err, "ZSLOG", PROFILE ) ;

	lg->set( log ) ;
	llog( "I", "Starting logger on " << log << endl ) ;

	log = p_poolMGR->sysget( err, "ZALOG", PROFILE ) ;
	lgx->set( log ) ;
	llog( "I", "Starting application logger on " << log << endl ) ;

	p_poolMGR->createSharedPool() ;

	ztlib = p_poolMGR->sysget( err, "ZTLIB", PROFILE ) ;

	p_poolMGR->sysput( err, "Z", "", SHARED )                  ;
	p_poolMGR->sysput( err, "ZSCRNAM1", "OFF", SHARED )        ;
	p_poolMGR->sysput( err, "ZSYSNAME", buf.sysname, SHARED )  ;
	p_poolMGR->sysput( err, "ZNODNAME", buf.nodename, SHARED ) ;
	p_poolMGR->sysput( err, "ZOSREL", buf.release, SHARED )    ;
	p_poolMGR->sysput( err, "ZOSVER", buf.version, SHARED )    ;
	p_poolMGR->sysput( err, "ZMACHINE", buf.machine, SHARED )  ;
	p_poolMGR->sysput( err, "ZDATEF",  "DD/MM/YY", SHARED )    ;
	p_poolMGR->sysput( err, "ZDATEFD", "DD/MM/YY", SHARED )    ;
	p_poolMGR->sysput( err, "ZSCALE", "OFF", SHARED )          ;
	p_poolMGR->sysput( err, "ZSPLIT", "NO", SHARED )           ;
	p_poolMGR->sysput( err, "ZNULLS", "NO", SHARED )           ;
	p_poolMGR->sysput( err, "ZCMPDATE", string(__DATE__), SHARED ) ;
	p_poolMGR->sysput( err, "ZCMPTIME", string(__TIME__), SHARED ) ;
	p_poolMGR->sysput( err, "ZENVIR", "lspf " + string(LSPF_VERSION), SHARED ) ;

	p_poolMGR->setPOOLsReadOnly() ;
	gmainpgm = p_poolMGR->sysget( err, "ZMAINPGM", PROFILE ) ;
}


string getEnvironmentVariable( const char* var )
{
	char* t = getenv( var ) ;

	if ( t == NULL )
	{
		llog( "I", "Environment variable "+ string( var ) +" has not been set"<< endl ) ;
		return "" ;
	}
	return t ;
}


void loadSystemCommandTable()
{
	// Terminate if ISPCMDS not found in ZUPROF

	string zuprof ;

	err.clear() ;

	zuprof  = getenv( "HOME" ) ;
	zuprof += ZUPROF ;
	p_tableMGR->loadTable( err, "ISPCMDS", NOWRITE, zuprof, SHARE ) ;
	if ( !err.RC0() )
	{
		llog( "C", "Loading of system command table ISPCMDS failed" <<endl ) ;
		llog( "C", "RC="<< err.getRC() <<"  Aborting startup" <<endl ) ;
		llog( "C", "Check path "+ zuprof << endl ) ;
		listErrorBlock( err ) ;
		abortStartup() ;
	}
	llog( "I", "Loaded system command table ISPCMDS" << endl ) ;
}


void loadRetrieveBuffer()
{
	uint rbsize ;

	string ifile  ;
	string inLine ;

	if ( p_poolMGR->sysget( err, "ZSRETP", PROFILE ) == "N" ) { return ; }

	ifile  = getenv( "HOME" ) ;
	ifile += ZUPROF      ;
	ifile += "/RETPLIST" ;

	std::ifstream fin( ifile.c_str() ) ;
	if ( !fin.is_open() ) { return ; }

	rbsize = ds2d( p_poolMGR->sysget( err, "ZRBSIZE", PROFILE ) ) ;
	if ( retrieveBuffer.capacity() != rbsize )
	{
		retrieveBuffer.rset_capacity( rbsize ) ;
	}

	llog( "I", "Reloading retrieve buffer" << endl ) ;

	while ( getline( fin, inLine ) )
	{
		retrieveBuffer.push_back( inLine ) ;
		if ( retrieveBuffer.size() == retrieveBuffer.capacity() ) { break ; }
	}
	fin.close() ;
}


void saveRetrieveBuffer()
{
	string ofile ;

	if ( retrieveBuffer.empty() || p_poolMGR->sysget( err, "ZSRETP", PROFILE ) == "N" ) { return ; }

	ofile  = getenv( "HOME" ) ;
	ofile += ZUPROF      ;
	ofile += "/RETPLIST" ;

	std::ofstream fout( ofile.c_str() ) ;

	if ( !fout.is_open() ) { return ; }

	llog( "I", "Saving retrieve buffer" << endl ) ;

	for ( size_t i = 0 ; i < retrieveBuffer.size() ; i++ )
	{
		fout << retrieveBuffer[ i ] << endl ;
	}
	fout.close() ;
}


string pfKeyValue( int c )
{
	// Return the value of a pfkey stored in the profile pool.  If it does not exist, VPUT a null value.

	int keyn ;

	string key ;
	string val ;

	err.clear() ;

	keyn = c - KEY_F( 0 ) ;
	key  = "ZPF" + d2ds( keyn, 2 ) ;
	val  = p_poolMGR->get( err, key, PROFILE ) ;
	if ( err.RC8() )
	{
		p_poolMGR->put( err, key, "", PROFILE ) ;
	}

	return val ;
}


void createpfKeyDefaults()
{
	err.clear() ;

	for ( int i = 1 ; i < 25 ; i++ )
	{
		p_poolMGR->put( err, "ZPF" + d2ds( i, 2 ), pfKeyDefaults[ i ], PROFILE ) ;
		if ( !err.RC0() )
		{
			llog( "E", "Error creating default key for task "<<err.taskid<<endl);
			listErrorBlock( err ) ;
		}
	}
}


string ControlKeyAction( char c )
{
	// Translate the control-key to a command (stored in ZCTRLx system profile variables)

	err.clear() ;

	string s = keyname( c ) ;
	string k = "ZCTRL"      ;

	k.push_back( s[ 1 ] ) ;

	return p_poolMGR->sysget( err, k, PROFILE ) ;
}


void lScreenDefaultSettings()
{
	// Set the default message setting for this logical screen
	// ZDEFM = Y show message id on messages
	//         N don't show message id on messages

	p_poolMGR->put( err, currScrn->screenId, "ZSHMSGID", p_poolMGR->sysget( err, "ZDEFM", PROFILE ) ) ;
}


void updateDefaultVars()
{
	err.clear() ;

	gmaxwait = ds2d( p_poolMGR->sysget( err, "ZMAXWAIT", PROFILE ) ) ;
	gmainpgm = p_poolMGR->sysget( err, "ZMAINPGM", PROFILE ) ;
	p_poolMGR->sysput( err, "ZSPLIT", pLScreen::screensTotal > 1 ? "YES" : "NO", SHARED ) ;

	field::field_paduchar = p_poolMGR->sysget( err, "ZPADC", PROFILE ).front() ;

	field::field_nulls    = ( p_poolMGR->sysget( err, "ZNULLS", SHARED ) == "YES" ) ;

	intens                = ( p_poolMGR->sysget( err, "ZHIGH", PROFILE ) == "Y" ) ? A_BOLD : 0 ;
	field::field_intens   = intens ;
	pPanel::panel_intens  = intens ;
	pdc::pdc_intens       = intens ;
	abc::abc_intens       = intens ;
	Box::box_intens       = intens ;
	text::text_intens     = intens ;
}


void createSharedPoolVars( const string& applid )
{
	err.clear() ;

	p_poolMGR->put( err, "ZSCREEN", string( 1, zscreen[ priScreen ] ), SHARED, SYSTEM ) ;
	p_poolMGR->put( err, "ZSCRNUM", d2ds( currScrn->screenId ), SHARED, SYSTEM ) ;
	p_poolMGR->put( err, "ZAPPLID", applid, SHARED, SYSTEM ) ;
	p_poolMGR->put( err, "ZPFKEY", "PF00", SHARED, SYSTEM ) ;

}


void updateReflist()
{
	// Check if .NRET is ON and has a valid field name.  If so, add file to the reflist using
	// application ZRFLPGM, parmameters PLA plus the field entry value.  Run task in the background.

	// Don't update REFLIST if the application has done a CONTROL REFLIST NOUPDATE (flag ControlRefUpdate=false)
	// or ISPS PROFILE variable ZRFURL is not set to YES

	string fname = currAppl->get_nretfield() ;

	err.clear() ;

	if ( fname == "" || !currAppl->ControlRefUpdate || p_poolMGR->sysget( err, "ZRFURL", PROFILE ) != "YES" )
	{
		return ;
	}

	if ( currAppl->currPanel->field_valid( fname ) )
	{
		selct.clear() ;
		selct.pgm     = p_poolMGR->sysget( err, "ZRFLPGM", PROFILE ) ;
		selct.parm    = "PLA " + currAppl->currPanel->field_getvalue( fname ) ;
		selct.newappl = ""    ;
		selct.newpool = false ;
		selct.passlib = false ;
		startApplicationBack( selct ) ;
	}
	else
	{
		llog( "E", "Invalid field "+ fname +" in .NRET panel statement" << endl ) ;
		issueMessage( "PSYS011Z" ) ;
	}
}


void lineOutput()
{
	// Write line output to the display.  Split line if longer than screen width.

	size_t i ;

	string t ;

	if ( linePosn == -1 )
	{
		currScrn->save_panel_stack() ;
		currScrn->clear() ;
		linePosn = 0 ;
		beep() ;
	}

	currScrn->show_busy() ;
	attrset( RED | A_BOLD ) ;
	t = currAppl->lineBuffer ;

	do
	{
		if ( linePosn == int( pLScreen::maxrow-1 ) )
		{
			lineOutput_end() ;
		}
		if ( t.size() > pLScreen::maxcol )
		{
			i = t.find_last_of( ' ', pLScreen::maxcol ) ;
			i = ( i == string::npos ) ? pLScreen::maxcol : i + 1 ;
			mvaddstr( linePosn++, 0, t.substr( 0, i ).c_str() ) ;
			t.erase( 0, i ) ;
		}
		else
		{
			mvaddstr( linePosn++, 0, t.c_str() ) ;
			t = "" ;
		}
	} while ( t.size() > 0 ) ;

	currScrn->OIA_update( priScreen, altScreen ) ;

	wnoutrefresh( stdscr ) ;
	wnoutrefresh( OIA ) ;
	doupdate() ;
}


void lineOutput_end()
{
	attrset( RED | A_BOLD ) ;
	mvaddstr( linePosn, 0, "***" ) ;

	currScrn->show_enter() ;
	wnoutrefresh( stdscr ) ;
	wnoutrefresh( OIA ) ;
	move( linePosn, 3 ) ;
	doupdate() ;

	while ( true )
	{
		if ( isActionKey( getch() ) ) { break ; }
	}

	linePosn = 0 ;
	currScrn->clear() ;
	currScrn->clear_status() ;
	currScrn->OIA_startTime() ;
}


string listLogicalScreens()
{
	// Mainline lspf cannot create application panels but let's make this as similar as possible

	int o ;
	int c ;

	uint i ;
	uint m ;
	string ln  ;
	string t   ;
	string act ;
	string w2  ;

	err.clear() ;

	WINDOW* swwin   ;
	PANEL*  swpanel ;

	pApplication* appl ;

	vector<pLScreen*>::iterator its ;
	vector<string>::iterator it ;
	vector<string> lslist ;

	swwin   = newwin( screenList.size() + 6, 80, 1, 1 ) ;
	swpanel = new_panel( swwin )  ;
	wattrset( swwin, cuaAttr[ AWF ] | intens ) ;
	box( swwin, 0, 0 ) ;
	mvwaddstr( swwin, 0, 34, " Task List " ) ;

	wattrset( swwin, cuaAttr[ PT ] | intens ) ;
	mvwaddstr( swwin, 1, 28, "Active Logical Sessions" ) ;

	wattrset( swwin, cuaAttr[ PIN ] | intens ) ;
	mvwaddstr( swwin, 3, 2, "ID  Name      Application  Applid  Panel Title/Description" ) ;

	currScrn->show_wait() ;

	m = 0 ;
	for ( i = 0, its = screenList.begin() ; its != screenList.end() ; its++, i++ )
	{
		appl = (*its)->application_get_current() ;
		ln   = d2ds( (*its)->get_screenNum() )         ;
		if      ( i == priScreen ) { ln += "*" ; m = i ; }
		else if ( i == altScreen ) { ln += "-"         ; }
		else                       { ln += " "         ; }
		t = appl->get_current_panelDesc() ;
		if ( t.size() > 42 )
		{
			t.replace( 20, t.size()-39, "..." ) ;
		}
		ln = left( ln, 4 ) +
		     left( appl->get_current_screenName(), 10 ) +
		     left( appl->get_appname(), 13 ) +
		     left( appl->get_applid(), 8  )  +
		     left( t, 42 ) ;
		lslist.push_back( ln ) ;
	}

	o = m         ;
	curs_set( 0 ) ;
	while ( true )
	{
		for ( i = 0, it = lslist.begin() ; it != lslist.end() ; it++, i++ )
		{
			wattrset( swwin, cuaAttr[ i == m ? PT : VOI ] | intens ) ;
			mvwaddstr( swwin, i+4, 2, it->c_str() )         ;
		}
		update_panels() ;
		doupdate()  ;
		c = getch() ;
		if ( c >= CTRL( 'a' ) && c <= CTRL( 'z' ) )
		{
			act = ControlKeyAction( c ) ;
			iupper( act ) ;
			if ( word( act, 1 ) == "SWAP" )
			{
				w2 = word( act, 2 ) ;
				if ( w2 == "LISTN" ) { c = KEY_DOWN ; }
				else if ( w2 == "LISTP" ) { c = KEY_UP ; }
			}
		}
		if ( c == KEY_ENTER || c == 13 ) { break ; }
		if ( c == KEY_UP )
		{
			m == 0 ? m = lslist.size() - 1 : m-- ;
		}
		else if ( c == KEY_DOWN || c == CTRL( 'i' ) )
		{
			m == lslist.size()-1 ? m = 0 : m++ ;
		}
		else if ( isActionKey( c ) )
		{
			m = o ;
			break ;
		}
	}

	del_panel( swpanel ) ;
	delwin( swwin )      ;
	curs_set( 1 )        ;
	currScrn->OIA_startTime() ;

	return d2ds( m+1 )   ;
}


void listRetrieveBuffer()
{
	// Mainline lspf cannot create application panels but let's make this as similar as possible

	int c ;
	int RC ;

	uint i ;
	uint m ;
	uint mx  ;

	string t  ;
	string ln ;

	WINDOW* rbwin   ;
	PANEL*  rbpanel ;

	vector<string> lslist ;
	vector<string>::iterator it ;

	err.clear() ;
	if ( retrieveBuffer.empty() )
	{
		issueMessage( "PSYS012B" ) ;
		return ;
	}

	mx = (retrieveBuffer.size() > pLScreen::maxrow-6) ? pLScreen::maxrow-6 : retrieveBuffer.size() ;

	rbwin   = newwin( mx+5, 60, 1, 1 ) ;
	rbpanel = new_panel( rbwin )  ;
	wattrset( rbwin, cuaAttr[ AWF ] | intens ) ;
	box( rbwin, 0, 0 ) ;
	mvwaddstr( rbwin, 0, 25, " Retrieve " ) ;

	wattrset( rbwin, cuaAttr[ PT ] | intens ) ;
	mvwaddstr( rbwin, 1, 17, "lspf Command Retrieve Panel" ) ;

	currScrn->show_wait() ;
	for ( i = 0 ; i < mx ; i++ )
	{
		t = retrieveBuffer[ i ] ;
		if ( t.size() > 52 )
		{
			t.replace( 20, t.size()-49, "..." ) ;
		}
		ln = left(d2ds( i+1 )+".", 4 ) + t ;
		lslist.push_back( ln ) ;
	}

	curs_set( 0 ) ;
	m = 0 ;
	while ( true )
	{
		for ( i = 0, it = lslist.begin() ; it != lslist.end() ; it++, i++ )
		{
			wattrset( rbwin, cuaAttr[ i == m ? PT : VOI ] | intens ) ;
			mvwaddstr( rbwin, i+3, 3, it->c_str() ) ;
		}
		update_panels() ;
		doupdate()  ;
		c = getch() ;
		if ( c == KEY_ENTER || c == 13 )
		{
			currAppl->currPanel->cmd_setvalue( retrieveBuffer[ m ] ) ;
			currAppl->currPanel->cursor_to_cmdfield( RC, retrieveBuffer[ m ].size()+1 ) ;
			break ;
		}
		else if ( c == KEY_UP )
		{
			m == 0 ? m = lslist.size() - 1 : m-- ;
		}
		else if ( c == KEY_DOWN || c == CTRL( 'i' ) )
		{
			m == lslist.size() - 1 ? m = 0 : m++ ;
		}
		else if ( isActionKey( c ) )
		{
			currAppl->currPanel->cursor_to_cmdfield( RC ) ;
			break ;
		}
	}

	del_panel( rbpanel ) ;
	delwin( rbwin )      ;
	curs_set( 1 )        ;

	currScrn->set_cursor( currAppl ) ;
	currScrn->OIA_startTime() ;
}


void listBackTasks()
{
	// List background tasks and tasks that have been moved to the timeout queue

	llog( "-", "Listing background tasks:" << endl ) ;
	llog( "-", "         Number of tasks. . . . "<< pApplicationBackground.size()<< endl ) ;
	llog( "-", " "<< endl ) ;

	mtx.lock() ;
	for ( auto it = pApplicationBackground.begin() ; it != pApplicationBackground.end() ; it++ )
	{
		llog( "-", "         "<< setw(8) << (*it)->get_appname() <<
		      "   Id: "<< setw(5) << (*it)->taskid() <<
		      "   Status: "<< ( (*it)->terminateAppl ? "Terminated" : "Running" ) <<endl ) ;
	}
	mtx.unlock() ;

	llog( "-", " "<< endl ) ;
	llog( "-", "Listing timed-out tasks:" << endl ) ;
	llog( "-", "         Number of tasks. . . . "<< pApplicationTimeout.size()<< endl ) ;
	llog( "-", " "<< endl ) ;

	for ( auto it = pApplicationTimeout.begin() ; it != pApplicationTimeout.end() ; it++ )
	{
		llog( "-", "         "<< setw(8) << (*it)->get_appname() <<
		      "   Id: "<< setw(5) << (*it)->taskid() << endl ) ;
	}
	llog( "-", "*********************************************************************************"<<endl ) ;
}


void autoUpdate()
{
	// Resume application every 1s and wait.
	// Check every 50ms to see if ESC(27) has been pressed - read all characters from the buffer.

	char c ;

	bool end_auto = false ;

	currScrn->show_auto() ;
	nodelay( stdscr, true ) ;
	curs_set( 0 ) ;

	while ( !end_auto )
	{
		currScrn->OIA_startTime() ;
		ResumeApplicationAndWait() ;
		currScrn->OIA_update( priScreen, altScreen ) ;
		wnoutrefresh( stdscr ) ;
		wnoutrefresh( OIA ) ;
		update_panels() ;
		doupdate() ;
		for ( int i = 0 ; i < 20 && !end_auto ; i++ )
		{
			boost::this_thread::sleep_for( boost::chrono::milliseconds( 50 ) ) ;
			do
			{
				c = getch() ;
				if ( c == CTRL( '[' ) ) { end_auto = true ; }
			} while ( c != -1 ) ;
		}
	}

	curs_set( 1 ) ;
	currScrn->clear_status() ;
	nodelay( stdscr, false ) ;

	currScrn->set_cursor( currAppl ) ;
}


int getScreenNameNum( const string& s )
{
	// Return the screen number of screen name 's'.  If not found, return 0.
	// If a full match is not found, try to match an abbreviation.

	int i ;
	int j ;

	vector<pLScreen*>::iterator its ;
	pApplication* appl ;

	for ( i = 1, j = 0, its = screenList.begin() ; its != screenList.end() ; its++, i++ )
	{
		appl = (*its)->application_get_current() ;
		if ( appl->get_current_screenName() == s )
		{
			return i ;
		}
		else if ( j == 0 && abbrev( appl->get_current_screenName(), s ) )
		{
			j = i ;
		}
	}

	return j ;
}


void lspfCallbackHandler( lspfCommand& lc )
{
	//  Issue commands from applications using lspfCallback() function
	//  Replies go into the reply vector

	string w1 ;
	string w2 ;

	vector<pLScreen*>::iterator its    ;
	map<string, appInfo>::iterator ita ;
	pApplication* appl                 ;

	lc.reply.clear() ;

	w1 = word( lc.Command, 1 ) ;
	w2 = word( lc.Command, 2 ) ;

	if ( lc.Command == "SWAP LIST" )
	{
		for ( its = screenList.begin() ; its != screenList.end() ; its++ )
		{
			appl = (*its)->application_get_current()       ;
			lc.reply.push_back( d2ds( (*its)->get_screenNum() ) ) ;
			lc.reply.push_back( appl->get_appname()   )    ;
		}
		lc.RC = 0 ;
	}
	else if ( lc.Command == "MODULE STATUS" )
	{
		for ( ita = apps.begin() ; ita != apps.end() ; ita++ )
		{
			lc.reply.push_back( ita->first ) ;
			lc.reply.push_back( ita->second.module ) ;
			lc.reply.push_back( ita->second.file   ) ;
			if ( ita->second.mainpgm )
			{
				lc.reply.push_back( "R/Not Reloadable" ) ;
			}
			else if ( ita->second.relPending )
			{
				lc.reply.push_back( "Reload Pending" ) ;
			}
			else if ( ita->second.errors )
			{
				lc.reply.push_back( "Errors" ) ;
			}
			else if ( ita->second.refCount > 0 )
			{
				lc.reply.push_back( "Running" ) ;
			}
			else if ( ita->second.dlopened )
			{
				lc.reply.push_back( "Loaded" ) ;
			}
			else
			{
				lc.reply.push_back( "Not Loaded" ) ;
			}
		}
		lc.RC = 0 ;
	}
	else if ( w1 == "MODREL" )
	{
		reloadDynamicClasses( w2 ) ;
		lc.RC = 0 ;
	}
	else
	{
		lc.RC = 20 ;
	}
}


void abortStartup()
{
	delete screenList[ 0 ] ;

	cout << "*********************************************************************" << endl ;
	cout << "*********************************************************************" << endl ;
	cout << "Aborting startup of lspf.  Check lspf and application logs for errors" << endl ;
	cout << "lspf log name. . . . . :"<< lg->logname() << endl ;
	cout << "Application log name . :"<< lgx->logname() << endl ;
	cout << "*********************************************************************" << endl ;
	cout << "*********************************************************************" << endl ;
	cout << endl ;

	delete lg  ;
	delete lgx ;
	abort() ;
}


void abnormalTermMessage()
{
	if ( currAppl->abnormalTimeout )
	{
		errorScreen( 2, "An application timeout has occured.  Increase ZMAXWAIT if necessary" ) ;
	}
	else if ( currAppl->abnormalEndForced )
	{
		errorScreen( 2, "A forced termination of the subtask has occured" ) ;
	}
	else if ( not currAppl->abnormalNoMsg )
	{
		errorScreen( 2, "An error has occured during application execution" ) ;
	}
}


void errorScreen( int etype, const string& msg )
{
	int l    ;
	string t ;

	llog( "E", msg << endl ) ;

	if ( currAppl->errPanelissued || currAppl->abnormalNoMsg  ) { return ; }

	currScrn->save_panel_stack() ;
	currScrn->clear() ;
	currScrn->show_enter() ;

	beep() ;
	attrset( WHITE | A_BOLD ) ;
	mvaddstr( 0, 0, msg.c_str() ) ;
	mvaddstr( 1, 0, "See lspf and application logs for possible further details of the error" ) ;
	l = 2 ;
	if ( etype == 2 )
	{
		t = "Failing application is " + currAppl->get_appname() + ", taskid=" + d2ds( currAppl->taskid() ) ;
		llog( "E", t << endl ) ;
		mvaddstr( l++, 0, "Depending on the error, application may still be running in the background.  Recommend restarting lspf." ) ;
		mvaddstr( l++, 0, t.c_str() ) ;
	}
	mvaddstr( l, 0, "***" ) ;
	while ( true )
	{
		if ( isActionKey( getch() ) ) { break ; }
	}

	linePosn = -1 ;
	currScrn->restore_panel_stack() ;
}


void errorScreen( const string& title, const string& src )
{
	// Show dialogue error screen PSYSER2 with the contents of the errblock.

	// This is done by starting application PPSP01A with a parm of PSYSER2 and
	// passing the error parameters via selobj.options.

	// Make sure err_struct is the same in application PPSP01A.

	selobj sel ;

	struct err_struct
	{
		string title ;
		string src   ;
		errblock err ;
	} errs ;

	errs.err    = err   ;
	errs.title  = title ;
	errs.src    = src   ;

	sel.pgm     = "PPSP01A" ;
	sel.parm    = "PSYSER2" ;
	sel.newappl = ""    ;
	sel.newpool = false ;
	sel.passlib = false ;
	sel.suspend = true  ;
	sel.options = (void*)&errs ;

	startApplication( sel ) ;
}


void errorScreen( const string& msg )
{
	// Show dialogue error screen PSYSER3 with message 'msg'

	// This is done by starting application PPSP01A with a parm of PSYSER3 plus the message id.
	// ZVAL1-ZVAL3 need to be put into the SHARED pool depending on the message issued.

	selobj sel ;

	sel.pgm     = "PPSP01A" ;
	sel.parm    = "PSYSER3 "+ msg ;
	sel.newappl = ""    ;
	sel.newpool = false ;
	sel.passlib = false ;
	sel.suspend = true  ;

	startApplication( sel ) ;
}


void issueMessage( const string& msg )
{
	err.clear() ;

	currAppl->save_errblock() ;
	currAppl->set_msg( msg ) ;
	err = currAppl->get_errblock() ;
	currAppl->restore_errblock()   ;
	if ( !err.RC0() )
	{
		errorScreen( 1, "Syntax error in message "+ msg +", message file or message not found" ) ;
		listErrorBlock( err ) ;
	}
}


void serviceCallError( errblock& err )
{
	llog( "E", "A Serive Call error has occured"<< endl ) ;
	listErrorBlock( err ) ;
}


void listErrorBlock( errblock& err )
{
	llog( "E", "Error msg  : "<< err.msg1 << endl )  ;
	llog( "E", "Error id   : "<< err.msgid << endl ) ;
	llog( "E", "Error RC   : "<< err.getRC() << endl ) ;
	llog( "E", "Error ZVAL1: "<< err.val1 << endl )  ;
	llog( "E", "Error ZVAL2: "<< err.val2 << endl )  ;
	llog( "E", "Error ZVAL3: "<< err.val3 << endl )  ;
	llog( "E", "Source     : "<< err.getsrc() << endl ) ;
}


bool isActionKey( int c )
{
	return ( ( c >= KEY_F(1)    && c <= KEY_F(24) )   ||
		 ( c >= CTRL( 'a' ) && c <= CTRL( 'z' ) ) ||
		   c == CTRL( '[' ) ||
		   c == KEY_NPAGE   ||
		   c == KEY_PPAGE   ||
		   c == KEY_ENTER ) ;
}


void actionSwap( uint& row, uint& col )
{
	int RC ;
	int l  ;

	uint i ;

	string w1 ;
	string w2 ;

	w1 = word( zparm, 1 ) ;
	w2 = word( zparm, 2 ) ;

	if ( zparm != "" && zparm != "NEXT" && zparm != "PREV" )
	{
		currAppl->currPanel->cursor_to_cmdfield( RC ) ;
		currScrn->set_cursor( currAppl ) ;
	}

	if ( findword( w1, "LIST LISTN LISTP" ) )
	{
		w1 = listLogicalScreens() ;
	}

	if ( currAppl->msg_issued_with_cmd() )
	{
		currAppl->clear_msg() ;
	}

	if ( pLScreen::screensTotal == 1 ) { return ; }

	currScrn->save_panel_stack() ;
	currAppl->store_scrname()    ;
	if ( w1 =="*" ) { w1 = d2ds( priScreen+1 ) ; }
	if ( w1 != "" && w1 != "NEXT" && w1 != "PREV" && isvalidName( w1 ) )
	{
		l = getScreenNameNum( w1 ) ;
		if ( l > 0 ) { w1 = d2ds( l ) ; }
	}
	if ( w2 != "" && isvalidName( w2 ) )
	{
		l = getScreenNameNum( w2 ) ;
		if ( l > 0 ) { w2 = d2ds( l ) ; }
	}

	if ( w1 == "NEXT" )
	{
		priScreen++ ;
		priScreen = (priScreen == pLScreen::screensTotal ? 0 : priScreen) ;
		if ( altScreen == priScreen )
		{
			altScreen = (altScreen == 0 ? (pLScreen::screensTotal - 1) : (altScreen - 1) ) ;
		}
	}
	else if ( w1 == "PREV" )
	{
		if ( priScreen == 0 )
		{
			priScreen = pLScreen::screensTotal - 1 ;
		}
		else
		{
			priScreen-- ;
		}
		if ( altScreen == priScreen )
		{
			altScreen = ((altScreen == pLScreen::screensTotal - 1) ? 0 : (altScreen + 1) ) ;
		}
	}
	else if ( datatype( w1, 'W' ) )
	{
		i = ds2d( w1 ) - 1 ;
		if ( i < pLScreen::screensTotal )
		{
			if ( i != priScreen )
			{
				if ( w2 == "*" || i == altScreen )
				{
					altScreen = priScreen ;
				}
				priScreen = i ;
			}
		}
		else
		{
			swap( priScreen, altScreen ) ;
		}
		if ( datatype( w2, 'W' ) && w1 != w2 )
		{
			i = ds2d( w2 ) - 1 ;
			if ( i != priScreen && i < pLScreen::screensTotal )
			{
				altScreen = i ;
			}
		}
	}
	else
	{
		swap( priScreen, altScreen ) ;
	}

	currScrn = screenList[ priScreen ] ;
	currScrn->OIA_startTime() ;
	currAppl = currScrn->application_get_current() ;

	err.settask( currAppl->taskid() ) ;
	p_poolMGR->put( err, "ZPANELID", currAppl->get_panelid(), SHARED, SYSTEM ) ;
	currScrn->restore_panel_stack() ;
	currAppl->display_pd( err ) ;
}


void actionTabKey( uint& row, uint& col )
{
	// Tab processsing:
	//     If a pull down is active, go to next pulldown
	//     If cursor on a field that supports field execution and is not on the first char, execute function
	//     Else act as a tab key to the next input field

	uint rw ;
	uint cl ;

	bool tab_next = true ;

	string field_name ;

	fieldExc fxc ;

	if ( currAppl->currPanel->pd_active() )
	{
		displayNextPullDown( row, col ) ;
	}
	else
	{
		field_name = currAppl->currPanel->field_getname( row, col ) ;
		if ( field_name != "" )
		{
			currAppl->currPanel->field_get_row_col( field_name, rw, cl ) ;
			if ( rw == row && cl < col )
			{
				fxc = currAppl->currPanel->field_getexec( field_name ) ;
				if ( fxc.fieldExc_command != "" )
				{
					executeFieldCommand( field_name, fxc, col ) ;
					tab_next = false ;
				}
			}
		}
		if ( tab_next )
		{
			currAppl->currPanel->field_tab_next( row, col ) ;
			currScrn->set_cursor( row, col ) ;
		}
	}
}


void displayNextPullDown( uint& row, uint& col )
{
	string msg ;

	currAppl->currPanel->display_next_pd( err, msg ) ;
	if ( err.error() )
	{
		errorScreen( 1, "Error processing pull-down menu." ) ;
		serviceCallError( err ) ;
		currAppl->set_cursor_home() ;
	}
	else if ( msg != "" )
	{
		issueMessage( msg ) ;
	}
	currScrn->set_cursor( currAppl ) ;
}


void executeFieldCommand( const string& field_name, const fieldExc& fxc, uint col )
{
	// Run application associated with a field when tab pressed or command FIELDEXC entered

	// Cursor position is stored in shared variable ZFECSRP
	// Data to be passed to the application (fieldExc_passed) are stored in shared vars ZFEDATAn

	int i  ;
	int ws ;

	uint cl ;

	string w1 ;

	if ( !selct.parse( err, subword( fxc.fieldExc_command, 2 ) ) )
	{
		llog( "E", "Error in FIELD SELECT command "+ fxc.fieldExc_command << endl ) ;
		issueMessage( "PSYS011K" ) ;
		return ;
	}

	selct.parm += " " + currAppl->currPanel->field_getvalue( field_name ) ;
	currAppl->reffield = field_name ;
	cl = currAppl->currPanel->field_get_col( field_name ) ;
	p_poolMGR->put( err, "ZFECSRP", d2ds( col - cl + 1 ), SHARED ) ;

	for ( ws = words( fxc.fieldExc_passed ), i = 1 ; i <= ws ; i++ )
	{
		w1 = word( fxc.fieldExc_passed, i ) ;
		p_poolMGR->put( err, "ZFEDATA" + d2ds( i ), currAppl->currPanel->field_getvalue( w1 ), SHARED ) ;
	}

	startApplication( selct ) ;
}


void getDynamicClasses()
{
	// Get modules of the form libABCDE.so from ZLDPATH concatination with name ABCDE and store in map apps
	// Duplicates are ignored with a warning messasge.
	// Terminate lspf if ZMAINPGM module not found as we cannot continue

	int i        ;
	int j        ;
	int pos1     ;

	string appl  ;
	string mod   ;
	string fname ;
	string paths ;
	string p     ;

	typedef vector<path> vec ;
	vec v        ;

	vec::const_iterator it ;

	err.clear() ;
	appInfo aI  ;

	const string e1( gmainpgm +" not found.  Check ZLDPATH is correct.  lspf terminating **" ) ;

	paths = p_poolMGR->sysget( err, "ZLDPATH", PROFILE ) ;
	j     = getpaths( paths ) ;
	for ( i = 1 ; i <= j ; i++ )
	{
		p = getpath( paths, i ) ;
		if ( is_directory( p ) )
		{
			llog( "I", "Searching directory "+ p +" for application classes" << endl ) ;
			copy( directory_iterator( p ), directory_iterator(), back_inserter( v ) ) ;
		}
		else
		{
			llog( "W", "Ignoring directory "+ p +"  Not found or not a directory." << endl ) ;
		}
	}

	for ( it = v.begin() ; it != v.end() ; ++it )
	{
		fname = it->string() ;
		p     = substr( fname, 1, (lastpos( "/", fname ) - 1) ) ;
		mod   = substr( fname, (lastpos( "/", fname ) + 1) )    ;
		pos1  = pos( ".so", mod ) ;
		if ( substr(mod, 1, 3 ) != "lib" || pos1 == 0 ) { continue ; }
		appl  = substr( mod, 4, (pos1 - 4) ) ;
		if ( apps.count( appl ) > 0 )
		{
			llog( "W", "Ignoring duplicate module "+ mod +" found in "+ p << endl ) ;
			continue ;
		}
		llog( "I", "Adding application "+ appl << endl ) ;
		aI.file       = fname ;
		aI.module     = mod   ;
		aI.mainpgm    = false ;
		aI.dlopened   = false ;
		aI.errors     = false ;
		aI.relPending = false ;
		aI.refCount   = 0     ;
		apps[ appl ]  = aI    ;
	}
	llog( "I", d2ds( apps.size() ) +" applications found and stored" << endl ) ;
	if ( apps.find( gmainpgm ) == apps.end() )
	{
		llog( "C", e1 << endl ) ;
		abortStartup()          ;
	}
}


void reloadDynamicClasses( string parm )
{
	// Reload modules (ALL, NEW or modname).  Ignore reload for modules currently in-use but set
	// pending flag to be checked when application terminates.

	int i        ;
	int j        ;
	int k        ;
	int pos1     ;

	string appl  ;
	string mod   ;
	string fname ;
	string paths ;
	string p     ;

	bool stored  ;

	err.clear()  ;

	typedef vector<path> vec ;
	vec v      ;
	appInfo aI ;

	vec::const_iterator it ;

	paths = p_poolMGR->sysget( err, "ZLDPATH", PROFILE ) ;
	j     = getpaths( paths ) ;
	for ( i = 1 ; i <= j ; i++ )
	{
		p = getpath( paths, i ) ;
		if ( is_directory( p ) )
		{
			llog( "I", "Searching directory "+ p +" for application classes" << endl ) ;
			copy( directory_iterator( p ), directory_iterator(), back_inserter( v ) ) ;
		}
		else
		{
			llog( "W", "Ignoring directory "+ p +"  Not found or not a directory." << endl ) ;
		}
	}
	if ( parm == "" ) { parm = "ALL" ; }

	i = 0 ;
	j = 0 ;
	k = 0 ;
	for ( it = v.begin() ; it != v.end() ; ++it )
	{
		fname = it->string() ;
		p     = substr( fname, 1, (lastpos( "/", fname ) - 1) ) ;
		mod   = substr( fname, (lastpos( "/", fname ) + 1) )    ;
		pos1  = pos( ".so", mod ) ;
		if ( substr(mod, 1, 3 ) != "lib" || pos1 == 0 ) { continue ; }
		appl  = substr( mod, 4, (pos1 - 4) ) ;
		llog( "I", "Found application "+ appl << endl ) ;
		stored = ( apps.find( appl ) != apps.end() ) ;

		if ( parm == "NEW" && stored ) { continue ; }
		if ( parm != "NEW" && parm != "ALL" && parm != appl ) { continue ; }
		if ( appl == gmainpgm ) { continue ; }
		if ( parm == appl && stored && !apps[ appl ].dlopened )
		{
			apps[ appl ].file = fname ;
			llog( "W", "Application "+ appl +" not loaded.  Ignoring action" << endl ) ;
			return ;
		}
		if ( stored )
		{
			apps[ appl ].file = fname ;
			if ( apps[ appl ].refCount > 0 )
			{
				llog( "W", "Application "+ appl +" in use.  Reload pending" << endl ) ;
				apps[ appl ].relPending = true ;
				continue ;
			}
			if ( apps[ appl ].dlopened )
			{
				if ( loadDynamicClass( appl ) )
				{
					llog( "I", "Loaded "+ appl +" (module "+ mod +") from "+ p << endl ) ;
					i++ ;
				}
				else
				{
					llog( "W", "Errors occured loading "+ appl << endl ) ;
					k++ ;
				}
			}
		}
		else
		{
			llog( "I", "Adding new module "+ appl << endl ) ;
			aI.file        = fname ;
			aI.module      = mod   ;
			aI.mainpgm     = false ;
			aI.dlopened    = false ;
			aI.errors      = false ;
			aI.relPending  = false ;
			aI.refCount    = 0     ;
			apps[ appl ]   = aI    ;
			j++ ;
		}
		if ( parm == appl ) { break ; }
	}

	issueMessage( "PSYS012G" ) ;
	llog( "I", d2ds( i ) +" applications reloaded" << endl ) ;
	llog( "I", d2ds( j ) +" new applications stored" << endl ) ;
	llog( "I", d2ds( k ) +" errors encounted" << endl ) ;
	if ( parm != "ALL" && parm != "NEW" )
	{
		if ( (i+j) == 0 )
		{
			llog( "W", "Application "+ parm +" not reloaded/stored" << endl ) ;
			issueMessage( "PSYS012I" ) ;
		}
		else
		{
			llog( "I", "Application "+ parm +" reloaded/stored" << endl )   ;
			issueMessage( "PSYS012H" ) ;
		}
	}

	llog( "I", d2ds( apps.size() ) + " applications currently stored" << endl ) ;
}


bool loadDynamicClass( const string& appl )
{
	// Load module related to application appl and retrieve address of maker and destroy symbols
	// Perform dlclose first if there has been a previous successful dlopen, or if an error is encountered

	// Routine only called if the refCount is zero

	string mod   ;
	string fname ;

	void* dlib  ;
	void* maker ;
	void* destr ;

	const char* dlsym_err ;

	mod   = apps[ appl ].module ;
	fname = apps[ appl ].file   ;
	apps[ appl ].errors = true  ;

	if ( apps[ appl ].dlopened )
	{
		llog( "I", "Closing "+ appl << endl ) ;
		if ( !unloadDynamicClass( apps[ appl ].dlib ) )
		{
			llog( "W", "dlclose has failed for "+ appl << endl ) ;
			return false ;
		}
		apps[ appl ].dlopened = false ;
		llog( "I", "Reloading module "+ appl << endl ) ;
	}

	dlerror() ;
	dlib = dlopen( fname.c_str(), RTLD_NOW ) ;
	if ( !dlib )
	{
		llog( "E", "Error loading "+ fname << endl )  ;
		llog( "E", "Error is " << dlerror() << endl ) ;
		llog( "E", "Module "+ mod +" will be ignored" << endl ) ;
		return false ;
	}

	dlerror() ;
	maker     = dlsym( dlib, "maker" ) ;
	dlsym_err = dlerror() ;
	if ( dlsym_err )
	{
		llog( "E", "Error loading symbol maker" << endl ) ;
		llog( "E", "Error is " << dlsym_err << endl )     ;
		llog( "E", "Module "+ mod +" will be ignored" << endl ) ;
		unloadDynamicClass( apps[ appl ].dlib ) ;
		return false ;
	}

	dlerror() ;
	destr     = dlsym( dlib, "destroy" ) ;
	dlsym_err = dlerror() ;
	if ( dlsym_err )
	{
		llog( "E", "Error loading symbol destroy" << endl ) ;
		llog( "E", "Error is " << dlsym_err << endl )       ;
		llog( "E", "Module "+ mod +" will be ignored" << endl ) ;
		unloadDynamicClass( apps[ appl ].dlib ) ;
		return false ;
	}

	debug1( fname +" loaded at " << dlib << endl ) ;
	debug1( "Maker at " << maker << endl ) ;
	debug1( "Destroyer at " << destr << endl ) ;

	apps[ appl ].dlib         = dlib  ;
	apps[ appl ].maker_ep     = maker ;
	apps[ appl ].destroyer_ep = destr ;
	apps[ appl ].mainpgm      = false ;
	apps[ appl ].errors       = false ;
	apps[ appl ].dlopened     = true  ;

	return true ;
}


bool unloadDynamicClass( void* dlib )
{
	int i  ;
	int rc ;

	for ( i = 0 ; i < 100 ; i++ )
	{
		try
		{
			rc = dlclose( dlib ) ;
		}
		catch (...)
		{
			llog( "E", "An exception has occured during dlclose" << endl ) ;
			return false ;
		}
		if ( rc != 0 ) { break ; }
	}
	if ( rc == 0 ) { return false ; }
	return true ;
}
