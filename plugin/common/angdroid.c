/*
 * File: angdroid.c
 * Purpose: Native Angband port for Android platform
 *
 * Copyright (c) 2009 David Barr, Sergey N. Belinsky
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"


/*
 * This file helps Angband work on non-existant computers.
 *
 * To use this file, use "Makefile.xxx", which defines USE_AND.
 *
 *
 * This file is intended to show one way to build a "visual module"
 * for Angband to allow it to work with a new system.  It does not
 * actually work, but if the code near "XXX XXX XXX" comments were
 * replaced with functional code, then it probably would.
 *
 * See "z-term.c" for info on the concept of the "generic terminal",
 * and for more comments about what this file must supply.
 *
 * There are two basic ways to port Angband to a new system.  The
 * first involves modifying the "main-gcu.c" and/or "main-x11.c"
 * files to support some version of "curses" and/or "X11" on your
 * machine, and to compile with the "USE_GCU" and/or "USE_X11"
 * compilation flags defined.  The second involves creating a
 * new "main-xxx.c" file, based on this sample file (or on any
 * existing "main-xxx.c" file), and comes in two flavors, based
 * on whether it contains a "main()" function (as in "main-mac.c"
 * and "main-win.c") or not (as in "main-gcu.c" or "main-x11.c").
 *
 * If the "main-xxx.c" file includes its own "main()" function,
 * then you should NOT link in the "main.c" file, and your "main()"
 * function must process any command line arguments, initialize the
 * "visual system", and call "play_game()" with appropriate arguments.
 *
 * If the "main-xxx.c" file does not include its own "main()"
 * function, then you must add some code to "main.c" which, if
 * the appropriate "USE_AND" compilation flag is defined, will
 * attempt to call the "init_and()" function in the "main-xxx.c"
 * file, which should initialize the "visual system" and return
 * zero if it was successful.  The "main()" function in "main.c"
 * will take care of processing command line arguments and then
 * calling "play_game()" with appropriate arguments.
 *
 * Note that the "util.c" file often contains functions which must
 * be modified in small ways for various platforms, even if you are
 * able to use the existing "main-gcu.c" and/or "main-x11.c" files,
 * in particular, the "file handling" functions may not work on all
 * systems.
 *
 * When you complete a port to a new system, you should email any
 * newly created files, and any changes made to existing files,
 * including "h-config.h", "config.h", and any of the "Makefile"
 * files, to "rr9@thangorodrim.net" for inclusion in the next version.
 *
 * Always choose a "three letter" naming scheme for "main-xxx.c"
 * and "Makefile.xxx" and such for consistency and simplicity.
 *
 *
 * Initial framework (and all code) by Ben Harrison (benh@phial.com).
 */


#ifdef USE_AND

#ifndef ANGDROID_TOME_PLUGIN
#include "main.h"
#endif
#ifdef ANGDROID_ANGBAND306_PLUGIN
#include "borg1.h"
#endif

#include "angdroid.h"
#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <string.h>

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "Angband", __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG  , "Angband", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO   , "Angband", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN   , "Angband", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR  , "Angband", __VA_ARGS__) 

/* FIXME __android_log_write or __android_log_print ??? */
#define LOG(msg) (__android_log_write(ANDROID_LOG_DEBUG, "Angband", (msg)));

/*
 * JNI staff
 */

/* JVM enviroment */
static JavaVM *jvm;
static JNIEnv *env;

/* Classes */
static jclass NativeWrapperClass;

/* Objects */
static jobject NativeWrapperObj;

/* Methods of TermView */
static jmethodID NativeWrapper_text;
static jmethodID NativeWrapper_wipe;
static jmethodID NativeWrapper_clear;
static jmethodID NativeWrapper_noise;
static jmethodID NativeWrapper_refresh;
static jmethodID NativeWrapper_getch;
static jmethodID NativeWrapper_postInvalidate;
static jmethodID NativeWrapper_setCursorXY;
static jmethodID NativeWrapper_setCursorVisible;
static jmethodID NativeWrapper_clearKeyBuffer;

static char android_files_path[1024];
static char android_savefile[50];
static int turn_save = 0;

/*
 * Extra data to associate with each "window"
 *
 * Each "window" is represented by a "term_data" structure, which
 * contains a "term" structure, which contains a pointer (t->data)
 * back to the term_data structure.
 */

typedef struct term_data term_data;

struct term_data
{
	term t;
	/* Other fields if needed XXX XXX XXX */
};

#if defined (ANGDROID_ANGBAND_PLUGIN) || defined (ANGDROID_NPP_PLUGIN)
#include "textui.h" 
//static game_command cmd = { CMD_NULL, 0 };
#endif

static bool new_game = FALSE;

/*
 * Number of "term_data" structures to support XXX XXX XXX
 *
 * You MUST support at least one "term_data" structure, and the
 * game will currently use up to eight "term_data" structures if
 * they are available.
 *
 * Actually, MAX_TERM_DATA is now defined as eight in 'defines.h'.
 *
 * If only one "term_data" structure is supported, then a lot of
 * the things that would normally go into a "term_data" structure
 * could be made into global variables instead.
 */
#define MAX_AND_TERM 1

/*
 * An array of "term_data" structures, one for each "sub-window"
 */
static term_data data[MAX_AND_TERM];

/*
 * Often, it is helpful to create an array of "color data" containing
 * a representation of the "angband_color_table" array in some "local" form.
 *
 * Often, the "Term_xtra(TERM_XTRA_REACT, 0)" hook is used to initialize
 * "color_data" from "angband_color_table".  XXX XXX XXX
 */
/*
static local_color_data_type color_data[MAX_COLORS];
 */

/*
static errr CheckEvents(int wait);
*/

#ifdef ANGDROID_TOME_PLUGIN
/*
 * The my_strcpy() function copies up to 'bufsize'-1 characters from 'src'
 * to 'buf' and NUL-terminates the result.  The 'buf' and 'src' strings may
 * not overlap.
 *
 * my_strcpy() returns strlen(src).  This makes checking for truncation
 * easy.  Example: if (my_strcpy(buf, src, sizeof(buf)) >= sizeof(buf)) ...;
 *
 * This function should be equivalent to the strlcpy() function in BSD.
 */
size_t my_strcpy(char *buf, const char *src, size_t bufsize)
{
	size_t len = strlen(src);
	size_t ret = len;

	/* Paranoia */
	if (bufsize == 0) return ret;

	/* Truncate */
	if (len >= bufsize) len = bufsize - 1;

	/* Copy the string and terminate it */
	(void)memcpy(buf, src, len);
	buf[len] = '\0';

	/* Return strlen(src) */
	return ret;
}


/*
 * The my_strcat() tries to append a string to an existing NUL-terminated string.
 * It never writes more characters into the buffer than indicated by 'bufsize' and
 * NUL-terminates the buffer.  The 'buf' and 'src' strings may not overlap.
 *
 * my_strcat() returns strlen(buf) + strlen(src).  This makes checking for
 * truncation easy.  Example:
 * if (my_strcat(buf, src, sizeof(buf)) >= sizeof(buf)) ...;
 *
 * This function should be equivalent to the strlcat() function in BSD.
 */
size_t my_strcat(char *buf, const char *src, size_t bufsize)
{
	size_t dlen = strlen(buf);

	/* Is there room left in the buffer? */
	if (dlen < bufsize - 1)
	{
		/* Append as much as possible  */
		return (dlen + my_strcpy(buf + dlen, src, bufsize - dlen));
	}
	else
	{
		/* Return without appending */
		return (dlen + strlen(src));
	}
}
#endif /* ANGDROID_TOME_PLUGIN */


/*** Function hooks needed by "Term" ***/


/*
 * Init a new "term"
 *
 * This function should do whatever is necessary to prepare a new "term"
 * for use by the "z-term.c" package.  This may include clearing the window,
 * preparing the cursor, setting the font/colors, etc.  Usually, this
 * function does nothing, and the "init_and()" function does it all.
 */
static void Term_init_and(term *t)
{
	term_data *td = (term_data*)(t->data);

	LOGD("Term_init_and");

	/* XXX XXX XXX */
}


/*
 * Nuke an old "term"
 *
 * This function is called when an old "term" is no longer needed.  It should
 * do whatever is needed to clean up before the program exits, such as wiping
 * the screen, restoring the cursor, fixing the font, etc.  Often this function
 * does nothing and lets the operating system clean up when the program quits.
 */
static void Term_nuke_and(term *t)
{
	term_data *td = (term_data*)(t->data);

	LOGD("Term_nuke_and");

	/* XXX XXX XXX */
}


/*
 * Do a "user action" on the current "term"
 *
 * This function allows the visual module to do implementation defined
 * things when the user activates the "system defined command" command.
 *
 * This function is normally not used.
 *
 * In general, this function should return zero if the action is successfully
 * handled, and non-zero if the action is unknown or incorrectly handled.
 */
static errr Term_user_and(int n)
{
	term_data *td = (term_data*)(Term->data);

	LOGD("Term_user_and");

	/* XXX XXX XXX */

	/* Unknown */
	return 1;
}


/*
 * Do a "special thing" to the current "term"
 *
 * This function must react to a large number of possible arguments, each
 * corresponding to a different "action request" by the "z-term.c" package,
 * or by the application itself.
 *
 * The "action type" is specified by the first argument, which must be a
 * constant of the form "TERM_XTRA_*" as given in "z-term.h", and the second
 * argument specifies the "information" for that argument, if any, and will
 * vary according to the first argument.
 *
 * In general, this function should return zero if the action is successfully
 * handled, and non-zero if the action is unknown or incorrectly handled.
 */
static errr Term_xtra_and(int n, int v)
{
	term_data *td = (term_data*)(Term->data);
	jint ret;
	int key;

	/* Analyze */
	switch (n)
	{
		case TERM_XTRA_EVENT:
		{
			/*
			 * Process some pending events XXX XXX XXX
			 *
			 * Wait for at least one event if "v" is non-zero
			 * otherwise, if no events are ready, return at once.
			 * When "keypress" events are encountered, the "ascii"
			 * value corresponding to the key should be sent to the
			 * "Term_keypress()" function.  Certain "bizarre" keys,
			 * such as function keys or arrow keys, may send special
			 * sequences of characters, such as control-underscore,
			 * plus letters corresponding to modifier keys, plus an
			 * underscore, plus carriage return, which can be used by
			 * the main program for "macro" triggers.  This action
			 * should handle as many events as is efficiently possible
			 * but is only required to handle a single event, and then
			 * only if one is ready or "v" is true.
			 *
			 * This action is required.
			 */
			
			key = (*env)->CallIntMethod(env, NativeWrapperObj, NativeWrapper_getch, v);
			if (key == -1) {
				LOGD("TERM_XTRA_EVENT.saving game");
				if (turn_save != turn 
					&& turn > 1 
					&& p_ptr != NULL 
					&& !p_ptr->is_dead) {
#if defined (ANGDROID_ANGBAND_PLUGIN) || defined (ANGDROID_NPP_PLUGIN)
					save_game();
#else
					if (!borg_active) do_cmd_save_game();
#endif
					turn_save = turn;
					LOGD("TERM_XTRA_EVENT.saved game success");
				}
				else {
					LOGD("TERM_XTRA_EVENT.save skipped");
				}
			}
			else if (v == 0) {
				while (key != 0)
				{
					Term_keypress(key);
					key = (*env)->CallIntMethod(env, NativeWrapperObj, NativeWrapper_getch, v);
				}
			}
			else {
				Term_keypress(key);
			}
			
			return 0;
		}

		case TERM_XTRA_FLUSH:
		{
			/*
			 * Flush all pending events XXX XXX XXX
			 *
			 * This action should handle all events waiting on the
			 * queue, optionally discarding all "keypress" events,
			 * since they will be discarded anyway in "z-term.c".
			 *
			 * This action is required, but may not be "essential".
			 */
			LOGD("TERM_XTRA_FLUSH");
			(*env)->CallVoidMethod(env, NativeWrapperObj, NativeWrapper_clearKeyBuffer);

			return 0;
		}

		case TERM_XTRA_CLEAR:
		{
			/*
			 * Clear the entire window XXX XXX XXX
			 *
			 * This action should clear the entire window, and redraw
			 * any "borders" or other "graphic" aspects of the window.
			 *
			 * This action is required.
			 */
			//LOGD("TERM_XTRA_CLEAR");
			(*env)->CallVoidMethod(env, NativeWrapperObj, NativeWrapper_clear);
			return 0;
		}

		case TERM_XTRA_SHAPE:
		{
			/*
			 * Set the cursor visibility XXX XXX XXX
			 *
			 * This action should change the visibility of the cursor,
			 * if possible, to the requested value (0=off, 1=on)
			 *
			 * This action is optional, but can improve both the
			 * efficiency (and attractiveness) of the program.
			 */
			//LOGD("TERM_XTRA_SHAPE");
			(*env)->CallVoidMethod(env, NativeWrapperObj, NativeWrapper_setCursorVisible, v);
			
			return 0;
		}

		case TERM_XTRA_FROSH:
		{
			/*
			 * Flush a row of output XXX XXX XXX
			 *
			 * This action should make sure that row "v" of the "output"
			 * to the window will actually appear on the window.
			 *
			 * This action is optional, assuming that "Term_text_and()"
			 * (and similar functions) draw directly to the screen, or
			 * that the "TERM_XTRA_FRESH" entry below takes care of any
			 * necessary flushing issues.
			 */
			LOGD("TERM_XTRA_FROSH");
			return 0;
		}

		case TERM_XTRA_FRESH:
		{
			/*
			 * Flush output XXX XXX XXX
			 *
			 * This action should make sure that all "output" to the
			 * window will actually appear on the window.
			 *
			 * This action is optional, assuming that "Term_text_and()"
			 * (and similar functions) draw directly to the screen, or
			 * that the "TERM_XTRA_FROSH" entry above takes care of any
			 * necessary flushing issues.
			 */
			//LOGD("TERM_XTRA_FRESH");
			(*env)->CallVoidMethod(env, NativeWrapperObj, NativeWrapper_postInvalidate);
			return 0;
		}

		case TERM_XTRA_NOISE:
		{
			/*
			 * Make a noise XXX XXX XXX
			 *
			 * This action should produce a "beep" noise.
			 *
			 * This action is optional, but convenient.
			 */
			LOGD("TERM_XTRA_NOISE");
			(*env)->CallVoidMethod(env, NativeWrapperObj, NativeWrapper_noise);
			return 0;
		}

		case TERM_XTRA_BORED:
		{
			/*
			 * Handle random events when bored XXX XXX XXX
			 *
			 * This action is optional, and normally not important
			 */
			LOGD("TERM_XTRA_BORED");
			return 0;
		}

		case TERM_XTRA_REACT:
		{
			/*
			 * React to global changes XXX XXX XXX
			 *
			 * For example, this action can be used to react to
			 * changes in the global "angband_color_table[MAX_COLORS][4]" array.
			 *
			 * This action is optional, but can be very useful for
			 * handling "color changes" and the "arg_sound" and/or
			 * "arg_graphics" options.
			 */
			LOGD("TERM_XTRA_REACT");
			
			return 0;
		}

		case TERM_XTRA_ALIVE:
		{
			/*
			 * Change the "hard" level XXX XXX XXX
			 *
			 * This action is used if the program changes "aliveness"
			 * by being either "suspended" (v=0) or "resumed" (v=1)
			 * This action is optional, unless the computer uses the
			 * same "physical screen" for multiple programs, in which
			 * case this action should clean up to let other programs
			 * use the screen, or resume from such a cleaned up state.
			 *
			 * This action is currently only used by "main-gcu.c",
			 * on UNIX machines, to allow proper "suspending".
			 */
			LOGD("TERM_XTRA_ALIVE");
			return 0;
		}

		case TERM_XTRA_LEVEL:
		{
			/*
			 * Change the "soft" level XXX XXX XXX
			 *
			 * This action is used when the term window changes "activation"
			 * either by becoming "inactive" (v=0) or "active" (v=1)
			 *
			 * This action can be used to do things like activate the proper
			 * font / drawing mode for the newly active term window.  This
			 * action should NOT change which window has the "focus", which
			 * window is "raised", or anything like that.
			 *
			 * This action is optional if all the other things which depend
			 * on what term is active handle activation themself, or if only
			 * one "term_data" structure is supported by this file.
			 */
			LOGD("TERM_XTRA_LEVEL");
			return 0;
		}

		case TERM_XTRA_DELAY:
		{
			/*
			 * Delay for some milliseconds XXX XXX XXX
			 *
			 * This action is useful for proper "timing" of certain
			 * visual effects, such as breath attacks.
			 *
			 * This action is optional, but may be required by this file,
			 * especially if special "macro sequences" must be supported.
			 */
			//LOGD("TERM_XTRA_DELAY v=%d", v);
			
			if (v > 0)
				usleep((unsigned int)1000 * v);
			
			return 0;
		}
	}

	/* Unknown or Unhandled action */
	return 1;
}


/*
 * Display the cursor
 *
 * This routine should display the cursor at the given location
 * (x,y) in some manner.  On some machines this involves actually
 * moving the physical cursor, on others it involves drawing a fake
 * cursor in some form of graphics mode.  Note the "soft_cursor"
 * flag which tells "z-term.c" to treat the "cursor" as a "visual"
 * thing and not as a "hardware" cursor.
 *
 * You may assume "valid" input if the window is properly sized.
 *
 * You may use the "Term_what(x, y, &a, &c)" function, if needed,
 * to determine what attr/char should be "under" the new cursor,
 * for "inverting" purposes or whatever.
 */
static errr Term_curs_and(int x, int y)
{
	term_data *td = (term_data*)(Term->data);

	//LOGD("Term_curs_and");

	/* XXX XXX XXX */
	(*env)->CallVoidMethod(env, NativeWrapperObj, NativeWrapper_setCursorXY, x , y);

	/* Success */
	return 0;
}


/*
 * Erase some characters
 *
 * This function should erase "n" characters starting at (x,y).
 *
 * You may assume "valid" input if the window is properly sized.
 */
static errr Term_wipe_and(int x, int y, int n)
{
	term_data *td = (term_data*)(Term->data);

	LOGD("Term_wipe_and");

	/* XXX XXX XXX */
	(*env)->CallVoidMethod(env, NativeWrapperObj, NativeWrapper_wipe, x , y , n);

	/* Success */
	return 0;
}


/*
 * Draw some text on the screen
 *
 * This function should actually display an array of characters
 * starting at the given location, using the given "attribute",
 * and using the given string of characters, which contains
 * exactly "n" characters and which is NOT null-terminated.
 *
 * You may assume "valid" input if the window is properly sized.
 *
 * You must be sure that the string, when written, erases anything
 * (including any visual cursor) that used to be where the text is
 * drawn.  On many machines this happens automatically, on others,
 * you must first call "Term_wipe_and()" to clear the area.
 *
 * In color environments, you should activate the color contained
 * in "color_data[a & BASIC_COLORS]", if needed, before drawing anything.
 *
 * You may ignore the "attribute" if you are only supporting a
 * monochrome environment, since this routine is normally never
 * called to display "black" (invisible) text, including the
 * default "spaces", and all other colors should be drawn in
 * the "normal" color in a monochrome environment.
 *
 * Note that if you have changed the "attr_blank" to something
 * which is not black, then this function must be able to draw
 * the resulting "blank" correctly.
 *
 * Note that this function must correctly handle "black" text if
 * the "always_text" flag is set, if this flag is not set, all the
 * "black" text will be handled by the "Term_wipe_and()" hook.
 */
static errr Term_text_and(int x, int y, int n, byte a, const char *cp)
{
	char tmp[1024];
	term_data *td = (term_data*)(Term->data);
	jint ret;

	jbyteArray array = (*env)->NewByteArray(env, n);
	
	if (array == NULL)
	{
		quit("Error: Out of memory");
	}

	(*env)->SetByteArrayRegion(env, array, 0, n, cp);

	ret = (*env)->CallIntMethod(env, NativeWrapperObj, NativeWrapper_text, x, y, n, a, array);

	(*env)->DeleteLocalRef(env, array);

	/* Success */
	return ret;
}

/*
 * Draw some attr/char pairs on the screen
 *
 * This routine should display the given "n" attr/char pairs at
 * the given location (x,y).  This function is only used if one
 * of the flags "always_pict" or "higher_pict" is defined.
 *
 * You must be sure that the attr/char pairs, when displayed, will
 * erase anything (including any visual cursor) that used to be at
 * the given location.  On many machines this is automatic, but on
 * others, you must first call "Term_wipe_and(x, y, 1)".
 *
 * With the "higher_pict" flag, this function can be used to allow
 * the display of "pseudo-graphic" pictures, for example, by using
 * the attr/char pair as an encoded index into a pixmap of special
 * "pictures".
 *
 * With the "always_pict" flag, this function can be used to force
 * every attr/char pair to be drawn by this function, which can be
 * very useful if this file can optimize its own display calls.
 *
 * This function is often associated with the "arg_graphics" flag.
 *
 * This function is only used if one of the "higher_pict" and/or
 * "always_pict" flags are set.
 */
static errr Term_pict_and(int x, int y, int n, const byte *ap, const char *cp,
                          const byte *tap, const char *tcp)
{
	term_data *td = (term_data*)(Term->data);

	LOGD("Term_pict_and");

	/* XXX XXX XXX */

	/* Success */
	return 0;
}


/*** Internal Functions ***/


/*
 * Instantiate a "term_data" structure
 *
 * This is one way to prepare the "term_data" structures and to
 * "link" the various informational pieces together.
 *
 * This function assumes that every window should be 80x24 in size
 * (the standard size) and should be able to queue 256 characters.
 * Technically, only the "main screen window" needs to queue any
 * characters, but this method is simple.  One way to allow some
 * variation is to add fields to the "term_data" structure listing
 * parameters for that window, initialize them in the "init_and()"
 * function, and then use them in the code below.
 *
 * Note that "activation" calls the "Term_init_and()" hook for
 * the "term" structure, if needed.
 */
static void term_data_link(int i)
{
	term_data *td = &data[i];
	term *t = &td->t;

	/* Initialize the term */
	term_init(t, 80, 24, 256);

	/* Choose "soft" or "hard" cursor XXX XXX XXX */
	/* A "soft" cursor must be explicitly "drawn" by the program */
	/* while a "hard" cursor has some "physical" existance and is */
	/* moved whenever text is drawn on the screen.  See "z-term.c". */
	t->soft_cursor = FALSE;

	/* Avoid the "corner" of the window XXX XXX XXX */
	/* t->icky_corner = TRUE; */

	/* Use "Term_pict()" for all attr/char pairs XXX XXX XXX */
	/* See the "Term_pict_and()" function above. */
	/* t->always_pict = TRUE; */

	/* Use "Term_pict()" for some attr/char pairs XXX XXX XXX */
	/* See the "Term_pict_and()" function above. */
	/* t->higher_pict = TRUE; */

	/* Use "Term_text()" even for "black" text XXX XXX XXX */
	/* See the "Term_text_and()" function above. */
	t->always_text = TRUE;

	/* Ignore the "TERM_XTRA_BORED" action XXX XXX XXX */
	/* This may make things slightly more efficient. */
	 t->never_bored = TRUE;

	/* Ignore the "TERM_XTRA_FROSH" action XXX XXX XXX */
	/* This may make things slightly more efficient. */
	t->never_frosh = TRUE;

	/* Erase with "white space" XXX XXX XXX */
	/* t->attr_blank = TERM_WHITE; */
	/* t->char_blank = ' '; */

	/* Prepare the init/nuke hooks */
	t->init_hook = Term_init_and;
	t->nuke_hook = Term_nuke_and;

	/* Prepare the template hooks */
	t->user_hook = Term_user_and;
	t->xtra_hook = Term_xtra_and;
	t->curs_hook = Term_curs_and;
	t->wipe_hook = Term_wipe_and;
	t->text_hook = Term_text_and;
#ifndef ANGDROID_TOME_PLUGIN
	t->pict_hook = Term_pict_and;
#endif
	/* Remember where we came from */
	t->data = td;

	/* Activate it */
	Term_activate(t);

	/* Global pointer */
	angband_term[i] = t;
}

static void hook_plog(cptr str)
{
	if (str)
	{
		LOGW(str);
	}
}


/*
 * Hook to tell the user something, and then quit 
 */
static void hook_quit(cptr str)
{
	if (str) LOGE(str);

	LOGD("hook_quit()");

	(*jvm)->DetachCurrentThread(jvm);

	pthread_exit(NULL);
}

/*
 * Help message.
 *   1st line = max 68 chars.
 *   Start next lines with 11 spaces
 */
/*
const char help_and[] = "Describe AND, subopts -describe suboptions here";
*/

/*
 * Initialization function
 */
errr init_and(void)
{
	int i;

	/* Initialize globals XXX XXX XXX */

	/* Initialize "term_data" structures XXX XXX XXX */

	/* Initialize the "color_data" array XXX XXX XXX */

	/* Create windows (backwards!) */
	for (i = MAX_AND_TERM; i-- > 0; )
	{
		/* Link */
		term_data_link(i);
	}

	/* Success */
	return 0;
}

/*
 * Some special machines need their own "main()" function, which they
 * can provide here, making sure NOT to compile the "main.c" file.
 *
 * These systems usually have some form of "event loop", run forever
 * as the last step of "main()", which handles things like menus and
 * window movement, and calls "play_game(FALSE)" to load a game after
 * initializing "savefile" to a filename, or "play_game(TRUE)" to make
 * a new game.  The event loop would also be triggered by "Term_xtra()"
 * (the TERM_XTRA_EVENT action), in which case the event loop would not
 * actually "loop", but would run once and return.
 */


/*
 * An event handler XXX XXX XXX
 *
 * You may need an event handler, which can be used by both
 * by the "TERM_XTRA_BORED" and "TERM_XTRA_EVENT" entries in
 * the "Term_xtra_and()" function, and also to wait for the
 * user to perform whatever user-interface operation is needed
 * to request the start of a new game or the loading of an old
 * game, both of which should launch the "play_game()" function.
 */
/* FIXME move code to here
static errr CheckEvents(int wait)
{
}
*/


/*
 * Make a sound.
 *
 * This action should produce sound number "v", where the
 * "name" of that sound is "sound_names[v]".
 *
 * This action is optional, and not very important.
 */
static void and_sound(int v)
{
	return;
}


/*
 * Init some stuff
 *
 * This function is used to keep the "path" variable off the stack.
 */


static void init_stuff()
{
	/* Prepare the path XXX XXX XXX */
	/* This must in some way prepare the "path" variable */
	/* so that it points at the "lib" directory.  Every */
	/* machine handles this in a different way... */
	// passed in android_files_path;

	/* Make sure it's terminated */
	android_files_path[511] = '\0';

	/* Hack -- Add a path separator (only if needed) */
	if (!suffix(android_files_path, PATH_SEP)) my_strcat(android_files_path, PATH_SEP, sizeof(android_files_path));

	LOGD(android_files_path);

	/* Prepare the filepaths */
#if defined (ANGDROID_ANGBAND_PLUGIN) || defined (ANGDROID_NPP_PLUGIN)
	init_file_paths(android_files_path, android_files_path, android_files_path);
	if (!file_exists(android_files_path))
	{
		/* Warning */
		plog_fmt("Unable to open the '%s' file.", android_files_path);
		quit("The Angband 'lib' folder is probably missing or misplaced.");
	}
#else
	init_file_paths(android_files_path);
#endif

	char temp[1024];
	strnfmt(temp, sizeof(temp), "%s", android_savefile);
	path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE, temp);

	process_player_name(FALSE);

#if defined (ANGDROID_ANGBAND_PLUGIN) || defined (ANGDROID_NPP_PLUGIN)
#ifdef USE_SOUND

	/* Set up sound hook */
	sound_hook = and_sound;

#endif /* USE_SOUND */
#endif /* ANGDROID_ANGBAND_PLUGIN */
}

#if defined (ANGDROID_ANGBAND_PLUGIN) || defined (ANGDROID_NPP_PLUGIN)
static errr get_init_cmd()
{
	/* Wait for response */
	pause_line(Term->hgt - 1);

	if (new_game)
		cmd_insert(CMD_NEWGAME);
	else
		/* This might be modified to supply the filename in future. */
		cmd_insert(CMD_LOADFILE);

	/* Everything's OK. */
	return 0;
}

/* Command dispatcher for android build */
static errr and_get_cmd(cmd_context context, bool wait)
{
	if (context == CMD_INIT)
		return get_init_cmd();
	else
		return textui_get_cmd(context, wait);
}
#endif /* ANGDROID_ANGBAND_PLUGIN */

bool private_check_user_directory(cptr dirpath)
{
	// todo: used in ToME figure out if we need it in android
	LOGD("private_check_user_directory %s",dirpath);
	return TRUE;
}

#ifdef ANDROID
void initGame ()
{
	LOGD("initGame");

	plog_aux = hook_plog;
	quit_aux = hook_quit;

	if (init_and() != 0) quit("Oops!");

	/* XXX XXX XXX */
	ANGBAND_SYS = "and";

#ifdef SET_UID

	/* Get the user id */
	player_uid = getuid();

	/* Save the effective GID for later recall */
	player_egid = getegid();

#endif /* SET_UID */

	/* Initialize some stuff */
	init_stuff();


	/* NativeWrapper Methods */
	NativeWrapper_text = (*env)->GetMethodID(env, NativeWrapperClass, "text", "(IIIB[B)I");
	NativeWrapper_wipe = (*env)->GetMethodID(env, NativeWrapperClass, "wipe", "(III)V");
	NativeWrapper_clear = (*env)->GetMethodID(env, NativeWrapperClass, "clear", "()V");
	NativeWrapper_noise = (*env)->GetMethodID(env, NativeWrapperClass, "noise", "()V");
	NativeWrapper_refresh = (*env)->GetMethodID(env, NativeWrapperClass, "refresh", "()V");
	NativeWrapper_getch = (*env)->GetMethodID(env, NativeWrapperClass, "getch", "(I)I");
	NativeWrapper_postInvalidate = (*env)->GetMethodID(env, NativeWrapperClass, "postInvalidate", "()V");
	NativeWrapper_setCursorXY = (*env)->GetMethodID(env, NativeWrapperClass, "setCursorXY", "(II)V");
	NativeWrapper_setCursorVisible = (*env)->GetMethodID(env, NativeWrapperClass, "setCursorVisible", "(I)V");
	NativeWrapper_clearKeyBuffer = (*env)->GetMethodID(env, NativeWrapperClass, "clearKeyBuffer", "()V");

#if defined (ANGDROID_ANGBAND_PLUGIN) || defined (ANGDROID_NPP_PLUGIN)
	/* Set up the command hook */
	cmd_get_hook = and_get_cmd;

	LOGD("init_display()");
	init_display();
#else
	LOGD("init_angband()");
	init_angband();
#endif /* ANGDROID_ANGBAND_PLUGIN */
}

JNIEXPORT jint JNICALL angdroid_gameQueryRedraw
(JNIEnv *env1, jobject obj1, jint x1, jint y1, jint x2, jint y2)
{
	LOGD("angdroid.angdroid_gameQueryRedraw %d %d %d %d", x1, y1, x2, y2);

	//return (jint)Term_redraw_section(x1, y1, x2, y2);
	return 0;
}

JNIEXPORT jstring JNICALL angdroid_gameQueryString
	(JNIEnv *env1, jobject obj1, jint argc, jobjectArray argv)
{
	return (jstring)0; // null indicates error
}

JNIEXPORT jint JNICALL angdroid_gameQueryInt
	(JNIEnv *env1, jobject obj1, jint argc, jobjectArray argv)
{
	jint result = -1; // -1 indicates error

	// process argc/argv 
	jstring argv0 = NULL;
	int i = 0;

	argv0 = (*env1)->GetObjectArrayElement(env1, argv, i);
	const char *copy_argv0 = (*env1)->GetStringUTFChars(env1, argv0, 0);

	if (strcmp(copy_argv0,"pv")==0) {
		result = 1;
	}
	else if (strcmp(copy_argv0,"px")==0) {
		result = p_ptr->px;
	}
	else if (strcmp(copy_argv0,"rl")==0) {
		result = 0;
#if defined (ANGDROID_ANGBAND_PLUGIN) 
		if (op_ptr && OPT(rogue_like_commands)) result=1;
#else
		if (rogue_like_commands) result=1;
#endif
	}
	else {
		result = -1; //unknown command
	}

	(*env1)->ReleaseStringUTFChars(env1, argv0, copy_argv0);

	return result;
}

JNIEXPORT void JNICALL angdroid_gameStart
(JNIEnv *env1, jobject obj1, jint argc, jobjectArray argv)
{
	env = env1;

	if ((*env)->GetJavaVM(env, &jvm) < 0)
	{
		LOGE("Error: Can't get JavaVM!");
	}

	LOGD("startGame()");

	(*jvm)->AttachCurrentThread(jvm, &env, NULL);

	// process argc/argv 
	jstring argv0 = NULL;
	int i;
	for(i = 0; i < argc; i++) {
		argv0 = (*env1)->GetObjectArrayElement(env1, argv, i);
		const char *copy_argv0 = (*env1)->GetStringUTFChars(env1, argv0, 0);

		LOGD("loader argv%d = %s",i,copy_argv0);

		switch(i){
		case 0: //files path
			my_strcpy(android_files_path, copy_argv0, strlen(copy_argv0)+1);
			break;
		case 1: //savefile
			my_strcpy(android_savefile, copy_argv0, strlen(copy_argv0)+1);
			break;
		default:
			break;
		}

		(*env1)->ReleaseStringUTFChars(env1, argv0, copy_argv0);
	}

	/* Save objects */
	NativeWrapperObj = obj1;

	/* Get NativeWrapper class */
	NativeWrapperClass = (*env)->GetObjectClass(env, NativeWrapperObj);

	initGame();

	/* Start main loop of game */
	LOGD("play_game()");
#if defined (ANGDROID_ANGBAND_PLUGIN) || defined (ANGDROID_NPP_PLUGIN)
	play_game();
#else
#ifdef ANGDROID_SANGBAND_PLUGIN
	/* Wait for response */
	pause_line(Term->rows - 1);
	play_game(FALSE);
#else
	/* Wait for response */
	pause_line(Term->hgt - 1);
	play_game(FALSE);
#endif /* ANGDROID_SANGBAND_PLUGIN */
#endif /* ANGDROID_ANGBAND_PLUGIN */

#ifndef ANGDROID_TOME_PLUGIN
	/* Free resources */
	LOGD("cleanup_angband()");
	cleanup_angband();
#endif

	quit("exit normally");
}

#endif

#endif /* USE_AND */