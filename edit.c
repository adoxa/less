/*
 * Copyright (c) 1984,1985,1989,1994  Mark Nudelman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice in the documentation and/or other materials provided with 
 *    the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN 
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "less.h"

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif

public int fd0 = 0;
#define	ISPIPE(fd)	((fd)==fd0)

extern int ispipe;
extern int new_file;
extern int errmsgs;
extern int quit_at_eof;
extern int file;
extern int cbufs;
extern char *every_first_cmd;
extern int any_display;
extern int force_open;
extern int is_tty;
extern IFILE curr_ifile;
extern IFILE old_ifile;
extern struct scrpos initial_scrpos;

#if LOGFILE
extern int logfile;
extern int force_logfile;
extern char *namelogfile;
#endif

static char *curr_altfilename = NULL;


/*
 * Textlist functions deal with a list of words separated by spaces.
 * init_textlist sets up a textlist structure.
 * forw_textlist uses that structure to iterate thru the list of
 * words, returning each one as a standard null-terminated string.
 * back_textlist does the same, but runs thru the list backwards.
 */
	public void
init_textlist(tlist, str)
	struct textlist *tlist;
	char *str;
{
	char *s;
	
	tlist->string = skipsp(str);
	tlist->endstring = tlist->string + strlen(tlist->string);
	for (s = str;  s < tlist->endstring;  s++)
	{
		if (*s == ' ')
			*s = '\0';
	}
}

	public char *
forw_textlist(tlist, prev)
	struct textlist *tlist;
	char *prev;
{
	char *s;
	
	/*
	 * prev == NULL means return the first word in the list.
	 * Otherwise, return the word after "prev".
	 */
	if (prev == NULL)
		s = tlist->string;
	else
		s = prev + strlen(prev);
	if (s >= tlist->endstring)
		return (NULL);
	while (*s == '\0')
		s++;
	if (s >= tlist->endstring)
		return (NULL);
	return (s);
}

	public char *
back_textlist(tlist, prev)
	struct textlist *tlist;
	char *prev;
{
	char *s;
	
	/*
	 * prev == NULL means return the last word in the list.
	 * Otherwise, return the word before "prev".
	 */
	if (prev == NULL)
		s = tlist->endstring;
	else if (prev <= tlist->string)
		return (NULL);
	else
		s = prev - 1;
	while (*s == '\0')
		s--;
	if (s <= tlist->string)
		return (NULL);
	while (s[-1] != '\0' && s > tlist->string)
		s--;
	return (s);
}


	static void
close_file()
{
	struct scrpos scrpos;
	
	if (curr_ifile == NULL_IFILE)
		return;
	/*
	 * Save the current position so that we can return to
	 * the same position if we edit this file again.
	 */
	get_scrpos(&scrpos);
	if (scrpos.pos != NULL_POSITION)
	{
		store_pos(curr_ifile, &scrpos);
		lastmark();
	}
	/*
	 * Close the current file, unless it is a pipe.
	 */
	if (!ISPIPE(file))
	{
		close(file);
		file = -1;
	}
	/*
	 * If we opened a file using an alternate name,
	 * do special stuff to close it.
	 */
	if (curr_altfilename != NULL)
	{
		close_altfile(curr_altfilename, get_filename(curr_ifile));
		free(curr_altfilename);
		curr_altfilename = NULL;
	}
	curr_ifile = NULL_IFILE;
}

/*
 * Edit a new file.
 * Filename == "-" means standard input.
 * Filename == NULL means just close the current file.
 */
	public int
edit(filename)
	char *filename;
{
	if (filename == NULL)
		return (edit_ifile(NULL_IFILE));
	return (edit_ifile(get_ifile(filename, curr_ifile)));
}
	
	public int
edit_ifile(ifile)
	IFILE ifile;
{
	int f;
	int answer;
	int no_display;
	char *filename;
	char *open_filename;
	char *alt_filename;
	PARG parg;
		
	if (ifile == NULL_IFILE)
	{
		/*
		 * Close the current file, but don't open a new one.
		 */
#if LOGFILE
		end_logfile();
#endif
		close_file();
		return (0);
	}
	if (ifile == curr_ifile)
	{
		/*
		 * Already have the correct file open.
		 */
		return (0);
	}
	filename = get_filename(ifile);
	alt_filename = open_altfile(filename);
	open_filename = (alt_filename != NULL) ? alt_filename : filename;
	
	if (strcmp(open_filename, "-") == 0)
	{
		/* 
		 * Use standard input.
		 */
		f = fd0;
	} else if ((parg.p_string = bad_file(open_filename)) != NULL)
	{
		error("%s", &parg);
		free(parg.p_string);
	    err1:
		if (alt_filename != NULL)
		{
			close_altfile(alt_filename, filename);
			free(alt_filename);
		}
		del_ifile(ifile);
		return (1);
	}
#if MSOFTC
	else if ((f = open(open_filename, O_RDONLY|O_BINARY)) < 0)
#else
	else if ((f = open(open_filename, 0)) < 0)
#endif
	{
		parg.p_string = errno_message(filename);
		error("%s", &parg);
		free(parg.p_string);
	    	goto err1;
	} else if (!force_open && !opened(ifile) && bin_file(f))
	{
		parg.p_string = filename;
		answer = query("\"%s\" may be a binary file.  See it anyway? ",
			&parg);
		if (answer != 'y' && answer != 'Y')
		{
			close(f);
			goto err1;
		}
	}

	ispipe = ISPIPE(f);
#if LOGFILE
{
	char *s;
	
	/*
	 *
	 */
	s = namelogfile;
	end_logfile();
	if (f >= 0 && ispipe && s != NULL && is_tty)
		use_logfile(s);
}
#endif

	/*
	 * We are now committed to using the new file.
	 * Close the current input file and set up to use the new one.
	 */
	if (curr_ifile != NULL_IFILE)
	{
		old_ifile = curr_ifile;
		close_file();
	}
	/*
	 * Get the new ifile.
	 * Get the saved position for the file.
	 */
	curr_ifile = ifile;
	file = f;
	curr_altfilename = alt_filename;
	set_open(curr_ifile); /* File has been opened */
	get_pos(curr_ifile, &initial_scrpos);

	if (ispipe)
		ch_pipe();
	else
		ch_nonpipe();
	(void) ch_nbuf(cbufs);
	ch_flush();

	new_file = 1;

	if (every_first_cmd != NULL)
		ungetsc(every_first_cmd);

	no_display = !any_display;
	flush();
	any_display = 1;

	if (is_tty)
	{
		/*
		 * Output is to a real tty.
		 */

		/*
		 * Indicate there is nothing displayed yet.
		 */
		pos_clear();
		clr_linenum();
		if (no_display && errmsgs > 0)
		{
			/*
			 * We displayed some messages on error output
			 * (file descriptor 2; see error() function).
			 * Before erasing the screen contents,
			 * display the file name and wait for a keystroke.
			 */
			parg.p_string = filename;
			error("%s", &parg);
		}
	}
	return (0);
}

/*
 * Edit a space-separated list of files.
 * For each filename in the list, enter it into the ifile list.
 * Then edit the first one.
 */
	public int
edit_list(filelist)
	char *filelist;
{
	IFILE save_curr_ifile;
	char *good_filename;
	char *filename;
	char *gfilelist;
	char *gfilename;
	struct textlist tl_files;
	struct textlist tl_gfiles;

	save_curr_ifile = curr_ifile;
	good_filename = NULL;
	
	/*
	 * Run thru each filename in the list.
	 * Try to glob the filename.  
	 * If it doesn't expand, just try to open the filename.
	 * If it does expand, try to open each name in that list.
	 */
	init_textlist(&tl_files, filelist);
	filename = NULL;
	while ((filename = forw_textlist(&tl_files, filename)) != NULL)
	{
		gfilelist = glob(filename);
		if (gfilelist == NULL)
		{
			if (edit(filename) == 0 && good_filename == NULL)
				good_filename = get_filename(curr_ifile);
		} else
		{
			init_textlist(&tl_gfiles, gfilelist);
			gfilename = NULL;
			while ((gfilename = forw_textlist(&tl_gfiles, gfilename)) != NULL)
			{
				if (edit(gfilename) == 0 && good_filename == NULL)
					good_filename = get_filename(curr_ifile);
			}
			free(gfilelist);
		}
	}
	/*
	 * Edit the first valid filename in the list.
	 */
	if (good_filename == NULL)
		return (1);
	if (get_ifile(good_filename, curr_ifile) == curr_ifile)
		/*
		 * Trying to edit the current file; don't reopen it.
		 */
		return (0);
	if (edit_ifile(save_curr_ifile))
		quit(-1);
	return (edit(good_filename));
}

/*
 * Edit the first file in the command line (ifile) list.
 */
	public int
edit_first()
{
	curr_ifile = NULL_IFILE;
	return (edit_next(1));
}

/*
 * Edit the last file in the command line (ifile) list.
 */
	public int
edit_last()
{
	curr_ifile = NULL_IFILE;
	return (edit_prev(1));
}


/*
 * Edit the next file in the command line (ifile) list.
 */
	public int
edit_next(n)
	int n;
{
	IFILE h;
	IFILE next;

	h = curr_ifile;
	/*
	 * Skip n filenames, then try to edit each filename.
	 */
	for (;;)
	{
		next = next_ifile(h);
		if (--n < 0)
		{
			if (edit_ifile(h) == 0)
				break;
		}
		if (next == NULL_IFILE)
		{
			/*
			 * Reached end of the ifile list.
			 */
			return (1);
		}
		h = next;
	} 
	/*
	 * Found a file that we can edit.
	 */
	return (0);
}

/*
 * Edit the previous file in the command line list.
 */
	public int
edit_prev(n)
	int n;
{
	IFILE h;
	IFILE next;

	h = curr_ifile;
	/*
	 * Skip n filenames, then try to edit each filename.
	 */
	for (;;)
	{
		next = prev_ifile(h);
		if (--n < 0)
		{
			if (edit_ifile(h) == 0)
				break;
		}
		if (next == NULL_IFILE)
		{
			/*
			 * Reached beginning of the ifile list.
			 */
			return (1);
		}
		h = next;
	} 
	/*
	 * Found a file that we can edit.
	 */
	return (0);
}

/*
 * Edit a specific file in the command line (ifile) list.
 */
	public int
edit_index(n)
	int n;
{
	IFILE h;

	h = NULL_IFILE;
	do
	{
		if ((h = next_ifile(h)) == NULL_IFILE)
		{
			/*
			 * Reached end of the list without finding it.
			 */
			return (1);
		}
	} while (get_index(h) != n);

	return (edit_ifile(h));
}

	public int
edit_stdin()
{
	if (isatty(fd0))
	{
#if MSOFTC
		error("Missing filename (\"less -?\" for help)", NULL_PARG);
#else
		error("Missing filename (\"less -\\?\" for help)", NULL_PARG);
#endif
		quit(0);
	}
	return (edit("-"));
}

/*
 * Copy a file directly to standard output.
 * Used if standard output is not a tty.
 */
	public void
cat_file()
{
	register int c;

	while ((c = ch_forw_get()) != EOI)
		putchr(c);
	flush();
}

#if LOGFILE

/*
 * If the user asked for a log file and our input file
 * is standard input, create the log file.  
 * We take care not to blindly overwrite an existing file.
 */
	public void
use_logfile(filename)
	char *filename;
{
	register int exists;
	register int answer;
	PARG parg;

	/*
	 * {{ We could use access() here. }}
	 */
	exists = open(filename, 0);
	close(exists);
	exists = (exists >= 0);

	/*
	 * Decide whether to overwrite the log file or append to it.
	 * (If it doesn't exist we "overwrite" it.
	 */
	if (!exists || force_logfile)
	{
		/*
		 * Overwrite (or create) the log file.
		 */
		answer = 'O';
	} else
	{
		/*
		 * Ask user what to do.
		 */
		parg.p_string = filename;
		answer = query("Warning: \"%s\" exists; Overwrite, Append or Don't log? ", &parg);
	}

loop:
	switch (answer)
	{
	case 'O': case 'o':
		/*
		 * Overwrite: create the file.
		 */
		logfile = creat(filename, 0644);
		break;
	case 'A': case 'a':
		/*
		 * Append: open the file and seek to the end.
		 */
#if MSOFTC
		logfile = open(filename, O_APPEND|O_WRONLY);
#else
		logfile = open(filename, 1);
#endif
		if (lseek(logfile, (off_t)0, 2) == BAD_LSEEK)
		{
			close(logfile);
			logfile = -1;
		}
		break;
	case 'D': case 'd':
		/*
		 * Don't do anything.
		 */
		return;
	case 'q':
		quit(0);
		/*NOTREACHED*/
	default:
		/*
		 * Eh?
		 */
		answer = query("Overwrite, Append, or Don't log? ", NULL_PARG);
		goto loop;
	}

	if (logfile < 0)
	{
		/*
		 * Error in opening logfile.
		 */
		parg.p_string = filename;
		error("Cannot write to \"%s\"", &parg);
	}
}

#endif