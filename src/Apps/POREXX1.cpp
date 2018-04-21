/*  Compile with ::                                                                                     */
/* g++ -shared -fPIC -std=c++11 -Wl,-soname,libPOREXX1.so -lrexx -lrexxapi -o libPOREXX1.so POREXX1.cpp */

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
/* OOREXX-lspf interface module                                                                         */
/*                                                                                                      */
/* Setup command handler and call REXX interpreter                                                      */
/* (command handler lspfServiceHandler used for address ISPEXEC rexx statements for lspf services)      */
/*                                                                                                      */
/* PARM word 1 is the rexx to invloke                                                                   */
/* PARM words 2 onwards are the parameters for the rexx (Arg(1))                                        */
/*                                                                                                      */
/* Search order:                                                                                        */
/*     Directory of REXX being run                                                                      */
/*     ZORXPATH                                                                                         */
/*     REXX_PATH env variable                                                                           */
/*     PATH      env variable                                                                           */
/*                                                                                                      */
/* Note that OOREXX will add extensions .rex and .REX when searching for the rexx to execute even       */
/* when a fully qualified name has been passed, and this will have precedence over the file name        */
/* without the extension ie. Search order is repeated without the extension.                            */
/*                                                                                                      */
/* Use system variable ZOREXPGM to refer to this module rather than the name, to allow different        */
/* rexx implementations to co-exist.                                                                    */

/*  RC/RSN codes returned                                                                               */
/*   0/0  Okay and ZRESULT set to result if not numeric                                                 */
/*  16/4  Missing rexx name                                                                             */
/*  20/condition.code ZRESULT is set to condition.message                                               */
/*  result/0 if no condition set and result is numeric                                                  */
/*                                                                                                      */
/* Take class-scope mutex lock around create interpreter and terminate() as these seem to hang when     */
/* they run together in background tasks (first terminate() hangs then everything attempting to         */
/* create a new interpreter instance)                                                                   */

#include <boost/filesystem.hpp>

#include "../lspf.h"
#include "../utilities.h"
#include "../classes.h"
#include "../pVPOOL.h"
#include "../pWidgets.h"
#include "../pTable.h"
#include "../pPanel.h"
#include "../pApplication.h"
#include "POREXX1.h"

#include <oorexxapi.h>
#include <oorexxerrors.h>


using namespace std ;
using namespace boost::filesystem ;

#undef  MOD_NAME
#define MOD_NAME POREXX1


RexxObjectPtr RexxEntry lspfServiceHandler( RexxExitContext*, RexxStringObject, RexxStringObject ) ;

int getRexxVariable( pApplication*, string, string & ) ;
int setRexxVariable( string, string ) ;
int getAllRexxVariables( pApplication* ) ;
int setAllRexxVariables( pApplication* ) ;

boost::mutex POREXX1::mtx ;

void POREXX1::application()
{
	llog( "I", "Application POREXX1 starting." << endl ) ;

	size_t version ;

	string rxpath  ;
	string msg     ;

	vget( "ZORXPATH", PROFILE ) ;
	vcopy( "ZORXPATH", rxpath, MOVE ) ;

	rexxName = word( PARM, 1 )    ;
	PARM     = subword( PARM, 2 ) ;

	if ( rexxName.size() > 0 && rexxName.front() == '%' ) { rexxName.erase( 0, 1 ) ; }

	if ( rexxName == "" )
	{
		llog( "E", "POREXX1 error. No REXX passed" << endl ) ;
		ZRC     = 16 ;
		ZRSN    = 4  ;
		ZRESULT = "No REXX passed" ;
		cleanup() ;
		return    ;
	}

	RexxInstance* instance   ;
	RexxThreadContext* threadContext ;
	RexxArrayObject args     ;
	RexxCondition condition  ;
	RexxDirectoryObject cond ;
	RexxObjectPtr result     ;

	RexxContextEnvironment environments[ 2 ] ;
	RexxOption             options[ 4 ]      ;

	environments[ 0 ].handler = lspfServiceHandler ;
	environments[ 0 ].name    = "ISPEXEC" ;
	environments[ 1 ].handler = NULL ;
	environments[ 1 ].name    = ""   ;

	options[ 0 ].optionName = APPLICATION_DATA ;
	options[ 0 ].option     = (void*)this      ;
	options[ 1 ].optionName = DIRECT_ENVIRONMENTS ;
	options[ 1 ].option     = (void*)environments ;
	options[ 2 ].optionName = EXTERNAL_CALL_PATH  ;
	options[ 2 ].option     = rxpath.c_str()      ;
	options[ 3 ].optionName = ""  ;

	lock() ;
	if ( RexxCreateInterpreter( &instance, &threadContext, options ) )
	{
		unlock() ;
		args = threadContext->NewArray( 1 ) ;
		threadContext->ArrayPut(args, threadContext->String( PARM.c_str() ), 1 ) ;
		version = threadContext->InterpreterVersion() ;

		if ( testMode )
		{
			llog( "I", "Starting OOREXX Interpreter Version. .: "<< version << endl ) ;
			llog( "I", "Running program. . . . . . . . . . . .: "<< rexxName << endl ) ;
			llog( "I", "With parameters. . . . . . . . . . . .: "<< PARM << endl ) ;
		}

		result = threadContext->CallProgram( rexxName.c_str(), args) ;

		if ( threadContext->CheckCondition() )
		{
			cond = threadContext->GetConditionInfo() ;
			threadContext->DecodeConditionInfo( cond, &condition ) ;
			llog( "E", "POREXX1 error running REXX.: " << rexxName << endl ) ;
			llog( "E", "   Condition Code . . . . .: " << condition.code << endl ) ;
			llog( "E", "   Condition Error Text . .: " << threadContext->CString( condition.errortext ) << endl ) ;
			llog( "E", "   Condition Message. . . .: " << threadContext->CString( condition.message ) << endl ) ;
			llog( "E", "   Line Error Occured . . .: " << condition.position << endl ) ;
			msg = "Error on line: "+ d2ds( condition.position ) ;
			msg = msg +".  Condition code: "+ d2ds( condition.code ) ;
			msg = msg +".  Error Text: "+ threadContext->CString( condition.errortext ) ;
			msg = msg +".  Message: "+ threadContext->CString( condition.message ) ;
			if ( msg.size() > 512 ) { msg.erase( 512 ) ; }
			vreplace( "STR", msg ) ;
			if ( condition.code == Rexx_Error_Program_unreadable_notfound )
			{
				setmsg( "PSYS012C" ) ;
			}
			else
			{
				setmsg( "PSYS011M" ) ;
			}
			ZRC     = 20 ;
			ZRSN    = condition.code ;
			ZRESULT = threadContext->CString( condition.message ) ;
		}
		else if ( result != NULLOBJECT )
		{
			ZRESULT = threadContext->CString( result ) ;
			if ( datatype( ZRESULT, 'W' ) )
			{
				ZRC     = ds2d( ZRESULT ) ;
				ZRESULT = "" ;
			}
		}
		lock() ;
		instance->Terminate() ;
	}
	unlock()  ;

	cleanup() ;
	return    ;
}


RexxObjectPtr RexxEntry lspfServiceHandler( RexxExitContext* context,
					    RexxStringObject address,
					    RexxStringObject command )
{
	// Called on address ISPEXEC REXX statement
	// Call lspf using the ispexec interface

	// On entry, update lspf function pool with the rexx variable pool values
	// On exit, update the rexx variable pool with the lspf function pool values

	int sRC   ;

	void* vptr ;

	string s = context->CString( command ) ;

	vptr = context->GetApplicationData() ;
	pApplication* thisAppl = static_cast<pApplication*>(vptr) ;

	getAllRexxVariables( thisAppl ) ;

	thisAppl->ispexec( s ) ;
	sRC = thisAppl->RC     ;

	setAllRexxVariables( thisAppl ) ;
	return context->WholeNumber( sRC ) ;
}


int getAllRexxVariables( pApplication* thisAppl )
{
	// For all variables in the rexx variable pool, set the lspf function pool variable.
	// Executed on entry to the command handler from a REXX procedure before any lspf
	// function is called

	// If the vdefine is for an INTEGER, convert value to a number and vreplace

	int rc ;

	string n ;
	string v ;

	const string& vl = thisAppl->vilist() ;

	SHVBLOCK var ;
	while ( true )
	{
		var.shvnext            = NULL ;
		var.shvname.strptr     = NULL ;     /* let REXX allocate the memory */
		var.shvname.strlength  = 0 ;
		var.shvnamelen         = 0 ;
		var.shvvalue.strptr    = NULL ;     /* let REXX allocate the memory */
		var.shvvalue.strlength = 0 ;
		var.shvvaluelen        = 0 ;
		var.shvcode            = RXSHV_NEXTV ;
		var.shvret             = 0 ;
		rc = RexxVariablePool( &var ) ;     /* get the variable             */
		if ( rc != RXSHV_OK) { break ; }
		n = string( var.shvname.strptr, var.shvname.strlength ) ;
		v = string( var.shvvalue.strptr, var.shvvalue.strlength ) ;
		if ( isvalidName( n ) )
		{
			if ( findword( n, vl ) )
			{
				thisAppl->vreplace( n, ds2d( v ) ) ;
			}
			else
			{
				thisAppl->vreplace( n, v ) ;
			}
		}
		RexxFreeMemory( (void*)var.shvname.strptr )  ;
		RexxFreeMemory( (void*)var.shvvalue.strptr ) ;
		if ( var.shvret & RXSHV_LVAR ) { break ; }
	}
	return rc ;
}


int setAllRexxVariables( pApplication* thisAppl )
{
	// For all variables in the application function pool, set the rexx variable.
	// Executed before returning to the REXX procedure after a call to the command handler
	// and after all lspf functions have been called

	// Note: vslist and vilist return the same variable reference

	int i  ;
	int ws ;
	int rc ;

	const string& vl = thisAppl->vslist() ;

	string w  ;
	string vi ;
	string* vs ;

	ws = words( vl ) ;
	for ( i = 1 ; i <= ws ; i++ )
	{
		w = word( vl, i ) ;
		thisAppl->vcopy( w, vs, LOCATE ) ;
		rc = setRexxVariable( w, (*vs) ) ;
	}

	thisAppl->vilist() ;
	ws = words( vl ) ;
	for ( i = 1 ; i <= ws ; i++ )
	{
		w = word( vl, i ) ;
		thisAppl->vcopy( w, vi, MOVE ) ;
		rc = setRexxVariable( w, vi ) ;
	}

	return rc ;
}


int getRexxVariable( pApplication* thisAppl, string n, string & v )
{
	// Get variable value from Rexx variable pool and update the application function pool

	int rc ;

	const char* name = n.c_str() ;

	SHVBLOCK var ;                       /* variable pool control block   */
	var.shvcode = RXSHV_SYFET ;          /* do a symbolic fetch operation */
	var.shvret  = 0 ;                    /* clear return code field       */
	var.shvnext = NULL ;                 /* no next block                 */
	var.shvvalue.strptr    = NULL ;      /* let REXX allocate the memory  */
	var.shvvalue.strlength = 0 ;
	var.shvvaluelen        = 0 ;
					     /* set variable name string      */
	MAKERXSTRING( var.shvname, name, n.size() ) ;

	rc = RexxVariablePool( &var ) ;
	if ( rc == 0 )
	{
		v = string( var.shvvalue.strptr, var.shvvalue.strlength ) ;
		thisAppl->vreplace( n, v ) ;
		rc = thisAppl->RC          ;
	}
	RexxFreeMemory( (void*)var.shvvalue.strptr ) ;
	return rc ;
}


int setRexxVariable( string n, string v )
{
	const char* name = n.c_str() ;
	char* value      = (char*)v.c_str() ;

	SHVBLOCK var ;                       /* variable pool control block   */
	var.shvcode = RXSHV_SYSET  ;         /* do a symbolic set operation   */
	var.shvret  = 0 ;                    /* clear return code field       */
	var.shvnext = NULL ;                 /* no next block                 */
					     /* set variable name string      */
	MAKERXSTRING( var.shvname, name, n.size() ) ;
					     /* set value string              */
	MAKERXSTRING( var.shvvalue, value, v.size() ) ;
	var.shvvaluelen = v.size()      ;    /* set value length              */
	return RexxVariablePool( &var ) ;
}


// ============================================================================================ //

extern "C" { pApplication* maker() { return new POREXX1 ; } }
extern "C" { void destroy(pApplication* p) { delete p ; } }

