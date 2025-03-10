/* vi:set ts=8 sts=4 sw=4:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * misc2.c: Various functions.
 */
#include "vim.h"

static char_u	*username = NULL; /* cached result of mch_get_user_name() */

static char_u	*ff_expand_buffer = NULL; /* used for expanding filenames */

#if defined(FEAT_VIRTUALEDIT) || defined(PROTO)
static int coladvance2 __ARGS((pos_T *pos, int addspaces, int finetune, colnr_T wcol));

/*
 * Return TRUE if in the current mode we need to use virtual.
 */
    int
virtual_active()
{
    /* While an operator is being executed we return "virtual_op", because
     * VIsual_active has already been reset, thus we can't check for "block"
     * being used. */
    if (virtual_op != MAYBE)
	return virtual_op;
    return (ve_flags == VE_ALL
# ifdef FEAT_VISUAL
	    || ((ve_flags & VE_BLOCK) && VIsual_active && VIsual_mode == Ctrl_V)
# endif
	    || ((ve_flags & VE_INSERT) && (State & INSERT)));
}

/*
 * Get the screen position of the cursor.
 */
    int
getviscol()
{
    colnr_T	x;

    getvvcol(curwin, &curwin->w_cursor, &x, NULL, NULL);
    return (int)x;
}

/*
 * Get the screen position of character col with a coladd in the cursor line.
 */
    int
getviscol2(col, coladd)
    colnr_T	col;
    colnr_T	coladd;
{
    colnr_T	x;
    pos_T	pos;

    pos.lnum = curwin->w_cursor.lnum;
    pos.col = col;
    pos.coladd = coladd;
    getvvcol(curwin, &pos, &x, NULL, NULL);
    return (int)x;
}

/*
 * Go to column "wcol", and add/insert white space as necessary to get the
 * cursor in that column.
 * The caller must have saved the cursor line for undo!
 */
    int
coladvance_force(wcol)
    colnr_T wcol;
{
    int rc = coladvance2(&curwin->w_cursor, TRUE, FALSE, wcol);

    if (wcol == MAXCOL)
	curwin->w_valid &= ~VALID_VIRTCOL;
    else
    {
	/* Virtcol is valid */
	curwin->w_valid |= VALID_VIRTCOL;
	curwin->w_virtcol = wcol;
    }
    return rc;
}
#endif

/*
 * Try to advance the Cursor to the specified screen column.
 * If virtual editing: fine tune the cursor position.
 * Note that all virtual positions off the end of a line should share
 * a curwin->w_cursor.col value (n.b. this is equal to STRLEN(line)),
 * beginning at coladd 0.
 *
 * return OK if desired column is reached, FAIL if not
 */
    int
coladvance(wcol)
    colnr_T	wcol;
{
    int rc = getvpos(&curwin->w_cursor, wcol);

    if (wcol == MAXCOL || rc == FAIL)
	curwin->w_valid &= ~VALID_VIRTCOL;
    else if (*ml_get_cursor() != TAB)
    {
	/* Virtcol is valid when not on a TAB */
	curwin->w_valid |= VALID_VIRTCOL;
	curwin->w_virtcol = wcol;
    }
    return rc;
}

/*
 * Return in "pos" the position of the cursor advanced to screen column "wcol".
 * return OK if desired column is reached, FAIL if not
 */
    int
getvpos(pos, wcol)
    pos_T   *pos;
    colnr_T wcol;
{
#ifdef FEAT_VIRTUALEDIT
    return coladvance2(pos, FALSE, virtual_active(), wcol);
}

    static int
coladvance2(pos, addspaces, finetune, wcol)
    pos_T	*pos;
    int		addspaces;	/* change the text to achieve our goal? */
    int		finetune;	/* change char offset for the exact column */
    colnr_T	wcol;		/* column to move to */
{
#endif
    int		idx;
    char_u	*ptr;
    char_u	*line;
    colnr_T	col = 0;
    int		csize = 0;
    int		one_more;
#ifdef FEAT_LINEBREAK
    int		head = 0;
#endif

    one_more = (State & INSERT)
		    || restart_edit != NUL
#ifdef FEAT_VISUAL
		    || (VIsual_active && *p_sel != 'o')
#endif
#ifdef FEAT_VIRTUALEDIT
		    || ((ve_flags & VE_ONEMORE) && wcol < MAXCOL)
#endif
		    ;
    line = ml_get_buf(curbuf, pos->lnum, FALSE);

    if (wcol >= MAXCOL)
    {
	    idx = (int)STRLEN(line) - 1 + one_more;
	    col = wcol;

#ifdef FEAT_VIRTUALEDIT
	    if ((addspaces || finetune) && !VIsual_active)
	    {
		curwin->w_curswant = linetabsize(line) + one_more;
		if (curwin->w_curswant > 0)
		    --curwin->w_curswant;
	    }
#endif
    }
    else
    {
#ifdef FEAT_VIRTUALEDIT
	int width = W_WIDTH(curwin) - win_col_off(curwin);

	if (finetune
		&& curwin->w_p_wrap
# ifdef FEAT_VERTSPLIT
		&& curwin->w_width != 0
# endif
		&& wcol >= (colnr_T)width)
	{
	    csize = linetabsize(line);
	    if (csize > 0)
		csize--;

	    if (wcol / width > (colnr_T)csize / width
		    && ((State & INSERT) == 0 || (int)wcol > csize + 1))
	    {
		/* In case of line wrapping don't move the cursor beyond the
		 * right screen edge.  In Insert mode allow going just beyond
		 * the last character (like what happens when typing and
		 * reaching the right window edge). */
		wcol = (csize / width + 1) * width - 1;
	    }
	}
#endif

	ptr = line;
	while (col <= wcol && *ptr != NUL)
	{
	    /* Count a tab for what it's worth (if list mode not on) */
#ifdef FEAT_LINEBREAK
	    csize = win_lbr_chartabsize(curwin, ptr, col, &head);
	    mb_ptr_adv(ptr);
#else
	    csize = lbr_chartabsize_adv(&ptr, col);
#endif
	    col += csize;
	}
	idx = (int)(ptr - line);
	/*
	 * Handle all the special cases.  The virtual_active() check
	 * is needed to ensure that a virtual position off the end of
	 * a line has the correct indexing.  The one_more comparison
	 * replaces an explicit add of one_more later on.
	 */
	if (col > wcol || (!virtual_active() && one_more == 0))
	{
	    idx -= 1;
# ifdef FEAT_LINEBREAK
	    /* Don't count the chars from 'showbreak'. */
	    csize -= head;
# endif
	    col -= csize;
	}

#ifdef FEAT_VIRTUALEDIT
	if (virtual_active()
		&& addspaces
		&& ((col != wcol && col != wcol + 1) || csize > 1))
	{
	    /* 'virtualedit' is set: The difference between wcol and col is
	     * filled with spaces. */

	    if (line[idx] == NUL)
	    {
		/* Append spaces */
		int	correct = wcol - col;
		char_u	*newline = alloc(idx + correct + 1);
		int	t;

		if (newline == NULL)
		    return FAIL;

		for (t = 0; t < idx; ++t)
		    newline[t] = line[t];

		for (t = 0; t < correct; ++t)
		    newline[t + idx] = ' ';

		newline[idx + correct] = NUL;

		ml_replace(pos->lnum, newline, FALSE);
		changed_bytes(pos->lnum, (colnr_T)idx);
		idx += correct;
		col = wcol;
	    }
	    else
	    {
		/* Break a tab */
		int	linelen = (int)STRLEN(line);
		int	correct = wcol - col - csize + 1; /* negative!! */
		char_u	*newline;
		int	t, s = 0;
		int	v;

		if (-correct > csize)
		    return FAIL;

		newline = alloc(linelen + csize);
		if (newline == NULL)
		    return FAIL;

		for (t = 0; t < linelen; t++)
		{
		    if (t != idx)
			newline[s++] = line[t];
		    else
			for (v = 0; v < csize; v++)
			    newline[s++] = ' ';
		}

		newline[linelen + csize - 1] = NUL;

		ml_replace(pos->lnum, newline, FALSE);
		changed_bytes(pos->lnum, idx);
		idx += (csize - 1 + correct);
		col += correct;
	    }
	}
#endif
    }

    if (idx < 0)
	pos->col = 0;
    else
	pos->col = idx;

#ifdef FEAT_VIRTUALEDIT
    pos->coladd = 0;

    if (finetune)
    {
	if (wcol == MAXCOL)
	{
	    /* The width of the last character is used to set coladd. */
	    if (!one_more)
	    {
		colnr_T	    scol, ecol;

		getvcol(curwin, pos, &scol, NULL, &ecol);
		pos->coladd = ecol - scol;
	    }
	}
	else
	{
	    int b = (int)wcol - (int)col;

	    /* The difference between wcol and col is used to set coladd. */
	    if (b > 0 && b < (MAXCOL - 2 * W_WIDTH(curwin)))
		pos->coladd = b;

	    col += b;
	}
    }
#endif

#ifdef FEAT_MBYTE
    /* prevent from moving onto a trail byte */
    if (has_mbyte)
	mb_adjustpos(curbuf, pos);
#endif

    if (col < wcol)
	return FAIL;
    return OK;
}

/*
 * Increment the cursor position.  See inc() for return values.
 */
    int
inc_cursor()
{
    return inc(&curwin->w_cursor);
}

/*
 * Increment the line pointer "lp" crossing line boundaries as necessary.
 * Return 1 when going to the next line.
 * Return 2 when moving forward onto a NUL at the end of the line).
 * Return -1 when at the end of file.
 * Return 0 otherwise.
 */
    int
inc(lp)
    pos_T  *lp;
{
    char_u  *p = ml_get_pos(lp);

    if (*p != NUL)	/* still within line, move to next char (may be NUL) */
    {
#ifdef FEAT_MBYTE
	if (has_mbyte)
	{
	    int l = (*mb_ptr2len)(p);

	    lp->col += l;
	    return ((p[l] != NUL) ? 0 : 2);
	}
#endif
	lp->col++;
#ifdef FEAT_VIRTUALEDIT
	lp->coladd = 0;
#endif
	return ((p[1] != NUL) ? 0 : 2);
    }
    if (lp->lnum != curbuf->b_ml.ml_line_count)     /* there is a next line */
    {
	lp->col = 0;
	lp->lnum++;
#ifdef FEAT_VIRTUALEDIT
	lp->coladd = 0;
#endif
	return 1;
    }
    return -1;
}

/*
 * incl(lp): same as inc(), but skip the NUL at the end of non-empty lines
 */
    int
incl(lp)
    pos_T    *lp;
{
    int	    r;

    if ((r = inc(lp)) >= 1 && lp->col)
	r = inc(lp);
    return r;
}

/*
 * dec(p)
 *
 * Decrement the line pointer 'p' crossing line boundaries as necessary.
 * Return 1 when crossing a line, -1 when at start of file, 0 otherwise.
 */
    int
dec_cursor()
{
    return dec(&curwin->w_cursor);
}

    int
dec(lp)
    pos_T  *lp;
{
    char_u	*p;

#ifdef FEAT_VIRTUALEDIT
    lp->coladd = 0;
#endif
    if (lp->col > 0)		/* still within line */
    {
	lp->col--;
#ifdef FEAT_MBYTE
	if (has_mbyte)
	{
	    p = ml_get(lp->lnum);
	    lp->col -= (*mb_head_off)(p, p + lp->col);
	}
#endif
	return 0;
    }
    if (lp->lnum > 1)		/* there is a prior line */
    {
	lp->lnum--;
	p = ml_get(lp->lnum);
	lp->col = (colnr_T)STRLEN(p);
#ifdef FEAT_MBYTE
	if (has_mbyte)
	    lp->col -= (*mb_head_off)(p, p + lp->col);
#endif
	return 1;
    }
    return -1;			/* at start of file */
}

/*
 * decl(lp): same as dec(), but skip the NUL at the end of non-empty lines
 */
    int
decl(lp)
    pos_T    *lp;
{
    int	    r;

    if ((r = dec(lp)) == 1 && lp->col)
	r = dec(lp);
    return r;
}

/*
 * Get the line number relative to the current cursor position, i.e. the
 * difference between line number and cursor position. Only look for lines that
 * can be visible, folded lines don't count.
 */
    linenr_T
get_cursor_rel_lnum(wp, lnum)
    win_T	*wp;
    linenr_T	lnum;		    /* line number to get the result for */
{
    linenr_T	cursor = wp->w_cursor.lnum;
    linenr_T	retval = 0;

#ifdef FEAT_FOLDING
    if (hasAnyFolding(wp))
    {
	if (lnum > cursor)
	{
	    while (lnum > cursor)
	    {
		(void)hasFolding(lnum, &lnum, NULL);
		/* if lnum and cursor are in the same fold,
		 * now lnum <= cursor */
		if (lnum > cursor)
		    retval++;
		lnum--;
	    }
	}
	else if (lnum < cursor)
	{
	    while (lnum < cursor)
	    {
		(void)hasFolding(lnum, NULL, &lnum);
		/* if lnum and cursor are in the same fold,
		 * now lnum >= cursor */
		if (lnum < cursor)
		    retval--;
		lnum++;
	    }
	}
	/* else if (lnum == cursor)
	 *     retval = 0;
	 */
    }
    else
#endif
	retval = lnum - cursor;

    return retval;
}

/*
 * Make sure curwin->w_cursor.lnum is valid.
 */
    void
check_cursor_lnum()
{
    if (curwin->w_cursor.lnum > curbuf->b_ml.ml_line_count)
    {
#ifdef FEAT_FOLDING
	/* If there is a closed fold at the end of the file, put the cursor in
	 * its first line.  Otherwise in the last line. */
	if (!hasFolding(curbuf->b_ml.ml_line_count,
						&curwin->w_cursor.lnum, NULL))
#endif
	    curwin->w_cursor.lnum = curbuf->b_ml.ml_line_count;
    }
    if (curwin->w_cursor.lnum <= 0)
	curwin->w_cursor.lnum = 1;
}

/*
 * Make sure curwin->w_cursor.col is valid.
 */
    void
check_cursor_col()
{
    check_cursor_col_win(curwin);
}

/*
 * Make sure win->w_cursor.col is valid.
 */
    void
check_cursor_col_win(win)
    win_T *win;
{
    colnr_T len;
#ifdef FEAT_VIRTUALEDIT
    colnr_T oldcol = win->w_cursor.col;
    colnr_T oldcoladd = win->w_cursor.col + win->w_cursor.coladd;
#endif

    len = (colnr_T)STRLEN(ml_get_buf(win->w_buffer, win->w_cursor.lnum, FALSE));
    if (len == 0)
	win->w_cursor.col = 0;
    else if (win->w_cursor.col >= len)
    {
	/* Allow cursor past end-of-line when:
	 * - in Insert mode or restarting Insert mode
	 * - in Visual mode and 'selection' isn't "old"
	 * - 'virtualedit' is set */
	if ((State & INSERT) || restart_edit
#ifdef FEAT_VISUAL
		|| (VIsual_active && *p_sel != 'o')
#endif
#ifdef FEAT_VIRTUALEDIT
		|| (ve_flags & VE_ONEMORE)
#endif
		|| virtual_active())
	    win->w_cursor.col = len;
	else
	{
	    win->w_cursor.col = len - 1;
#ifdef FEAT_MBYTE
	    /* Move the cursor to the head byte. */
	    if (has_mbyte)
		mb_adjustpos(win->w_buffer, &win->w_cursor);
#endif
	}
    }
    else if (win->w_cursor.col < 0)
	win->w_cursor.col = 0;

#ifdef FEAT_VIRTUALEDIT
    /* If virtual editing is on, we can leave the cursor on the old position,
     * only we must set it to virtual.  But don't do it when at the end of the
     * line. */
    if (oldcol == MAXCOL)
	win->w_cursor.coladd = 0;
    else if (ve_flags == VE_ALL)
    {
	if (oldcoladd > win->w_cursor.col)
	    win->w_cursor.coladd = oldcoladd - win->w_cursor.col;
	else
	    /* avoid weird number when there is a miscalculation or overflow */
	    win->w_cursor.coladd = 0;
    }
#endif
}

/*
 * make sure curwin->w_cursor in on a valid character
 */
    void
check_cursor()
{
    check_cursor_lnum();
    check_cursor_col();
}

#if defined(FEAT_TEXTOBJ) || defined(PROTO)
/*
 * Make sure curwin->w_cursor is not on the NUL at the end of the line.
 * Allow it when in Visual mode and 'selection' is not "old".
 */
    void
adjust_cursor_col()
{
    if (curwin->w_cursor.col > 0
# ifdef FEAT_VISUAL
	    && (!VIsual_active || *p_sel == 'o')
# endif
	    && gchar_cursor() == NUL)
	--curwin->w_cursor.col;
}
#endif

/*
 * When curwin->w_leftcol has changed, adjust the cursor position.
 * Return TRUE if the cursor was moved.
 */
    int
leftcol_changed()
{
    long	lastcol;
    colnr_T	s, e;
    int		retval = FALSE;

    changed_cline_bef_curs();
    lastcol = curwin->w_leftcol + W_WIDTH(curwin) - curwin_col_off() - 1;
    validate_virtcol();

    /*
     * If the cursor is right or left of the screen, move it to last or first
     * character.
     */
    if (curwin->w_virtcol > (colnr_T)(lastcol - p_siso))
    {
	retval = TRUE;
	coladvance((colnr_T)(lastcol - p_siso));
    }
    else if (curwin->w_virtcol < curwin->w_leftcol + p_siso)
    {
	retval = TRUE;
	(void)coladvance((colnr_T)(curwin->w_leftcol + p_siso));
    }

    /*
     * If the start of the character under the cursor is not on the screen,
     * advance the cursor one more char.  If this fails (last char of the
     * line) adjust the scrolling.
     */
    getvvcol(curwin, &curwin->w_cursor, &s, NULL, &e);
    if (e > (colnr_T)lastcol)
    {
	retval = TRUE;
	coladvance(s - 1);
    }
    else if (s < curwin->w_leftcol)
    {
	retval = TRUE;
	if (coladvance(e + 1) == FAIL)	/* there isn't another character */
	{
	    curwin->w_leftcol = s;	/* adjust w_leftcol instead */
	    changed_cline_bef_curs();
	}
    }

    if (retval)
	curwin->w_set_curswant = TRUE;
    redraw_later(NOT_VALID);
    return retval;
}

/**********************************************************************
 * Various routines dealing with allocation and deallocation of memory.
 */

#if defined(MEM_PROFILE) || defined(PROTO)

# define MEM_SIZES  8200
static long_u mem_allocs[MEM_SIZES];
static long_u mem_frees[MEM_SIZES];
static long_u mem_allocated;
static long_u mem_freed;
static long_u mem_peak;
static long_u num_alloc;
static long_u num_freed;

static void mem_pre_alloc_s __ARGS((size_t *sizep));
static void mem_pre_alloc_l __ARGS((long_u *sizep));
static void mem_post_alloc __ARGS((void **pp, size_t size));
static void mem_pre_free __ARGS((void **pp));

    static void
mem_pre_alloc_s(sizep)
    size_t *sizep;
{
    *sizep += sizeof(size_t);
}

    static void
mem_pre_alloc_l(sizep)
    long_u *sizep;
{
    *sizep += sizeof(size_t);
}

    static void
mem_post_alloc(pp, size)
    void **pp;
    size_t size;
{
    if (*pp == NULL)
	return;
    size -= sizeof(size_t);
    *(long_u *)*pp = size;
    if (size <= MEM_SIZES-1)
	mem_allocs[size-1]++;
    else
	mem_allocs[MEM_SIZES-1]++;
    mem_allocated += size;
    if (mem_allocated - mem_freed > mem_peak)
	mem_peak = mem_allocated - mem_freed;
    num_alloc++;
    *pp = (void *)((char *)*pp + sizeof(size_t));
}

    static void
mem_pre_free(pp)
    void **pp;
{
    long_u size;

    *pp = (void *)((char *)*pp - sizeof(size_t));
    size = *(size_t *)*pp;
    if (size <= MEM_SIZES-1)
	mem_frees[size-1]++;
    else
	mem_frees[MEM_SIZES-1]++;
    mem_freed += size;
    num_freed++;
}

/*
 * called on exit via atexit()
 */
    void
vim_mem_profile_dump()
{
    int i, j;

    printf("\r\n");
    j = 0;
    for (i = 0; i < MEM_SIZES - 1; i++)
    {
	if (mem_allocs[i] || mem_frees[i])
	{
	    if (mem_frees[i] > mem_allocs[i])
		printf("\r\n%s", _("ERROR: "));
	    printf("[%4d / %4lu-%-4lu] ", i + 1, mem_allocs[i], mem_frees[i]);
	    j++;
	    if (j > 3)
	    {
		j = 0;
		printf("\r\n");
	    }
	}
    }

    i = MEM_SIZES - 1;
    if (mem_allocs[i])
    {
	printf("\r\n");
	if (mem_frees[i] > mem_allocs[i])
	    puts(_("ERROR: "));
	printf("[>%d / %4lu-%-4lu]", i, mem_allocs[i], mem_frees[i]);
    }

    printf(_("\n[bytes] total alloc-freed %lu-%lu, in use %lu, peak use %lu\n"),
	    mem_allocated, mem_freed, mem_allocated - mem_freed, mem_peak);
    printf(_("[calls] total re/malloc()'s %lu, total free()'s %lu\n\n"),
	    num_alloc, num_freed);
}

#endif /* MEM_PROFILE */

/*
 * Some memory is reserved for error messages and for being able to
 * call mf_release_all(), which needs some memory for mf_trans_add().
 */
#if defined(MSDOS) && !defined(DJGPP)
# define SMALL_MEM
# define KEEP_ROOM 8192L
#else
# define KEEP_ROOM (2 * 8192L)
#endif

/*
 * Note: if unsigned is 16 bits we can only allocate up to 64K with alloc().
 * Use lalloc for larger blocks.
 */
    char_u *
alloc(size)
    unsigned	    size;
{
    return (lalloc((long_u)size, TRUE));
}

/*
 * Allocate memory and set all bytes to zero.
 */
    char_u *
alloc_clear(size)
    unsigned	    size;
{
    char_u *p;

    p = lalloc((long_u)size, TRUE);
    if (p != NULL)
	(void)vim_memset(p, 0, (size_t)size);
    return p;
}

/*
 * alloc() with check for maximum line length
 */
    char_u *
alloc_check(size)
    unsigned	    size;
{
#if !defined(UNIX) && !defined(__EMX__)
    if (sizeof(int) == 2 && size > 0x7fff)
    {
	/* Don't hide this message */
	emsg_silent = 0;
	EMSG(_("E340: Line is becoming too long"));
	return NULL;
    }
#endif
    return (lalloc((long_u)size, TRUE));
}

/*
 * Allocate memory like lalloc() and set all bytes to zero.
 */
    char_u *
lalloc_clear(size, message)
    long_u	size;
    int		message;
{
    char_u *p;

    p = (lalloc(size, message));
    if (p != NULL)
	(void)vim_memset(p, 0, (size_t)size);
    return p;
}

/*
 * Low level memory allocation function.
 * This is used often, KEEP IT FAST!
 */
    char_u *
lalloc(size, message)
    long_u	size;
    int		message;
{
    char_u	*p;		    /* pointer to new storage space */
    static int	releasing = FALSE;  /* don't do mf_release_all() recursive */
    int		try_again;
#if defined(HAVE_AVAIL_MEM) && !defined(SMALL_MEM)
    static long_u allocated = 0;    /* allocated since last avail check */
#endif

    /* Safety check for allocating zero bytes */
    if (size == 0)
    {
	/* Don't hide this message */
	emsg_silent = 0;
	EMSGN(_("E341: Internal error: lalloc(%ld, )"), size);
	return NULL;
    }

#ifdef MEM_PROFILE
    mem_pre_alloc_l(&size);
#endif

#if defined(MSDOS) && !defined(DJGPP)
    if (size >= 0xfff0)		/* in MSDOS we can't deal with >64K blocks */
	p = NULL;
    else
#endif

    /*
     * Loop when out of memory: Try to release some memfile blocks and
     * if some blocks are released call malloc again.
     */
    for (;;)
    {
	/*
	 * Handle three kind of systems:
	 * 1. No check for available memory: Just return.
	 * 2. Slow check for available memory: call mch_avail_mem() after
	 *    allocating KEEP_ROOM amount of memory.
	 * 3. Strict check for available memory: call mch_avail_mem()
	 */
	if ((p = (char_u *)malloc((size_t)size)) != NULL)
	{
#ifndef HAVE_AVAIL_MEM
	    /* 1. No check for available memory: Just return. */
	    goto theend;
#else
# ifndef SMALL_MEM
	    /* 2. Slow check for available memory: call mch_avail_mem() after
	     *    allocating (KEEP_ROOM / 2) amount of memory. */
	    allocated += size;
	    if (allocated < KEEP_ROOM / 2)
		goto theend;
	    allocated = 0;
# endif
	    /* 3. check for available memory: call mch_avail_mem() */
	    if (mch_avail_mem(TRUE) < KEEP_ROOM && !releasing)
	    {
		free((char *)p);	/* System is low... no go! */
		p = NULL;
	    }
	    else
		goto theend;
#endif
	}
	/*
	 * Remember that mf_release_all() is being called to avoid an endless
	 * loop, because mf_release_all() may call alloc() recursively.
	 */
	if (releasing)
	    break;
	releasing = TRUE;

	clear_sb_text();	      /* free any scrollback text */
	try_again = mf_release_all(); /* release as many blocks as possible */
#ifdef FEAT_EVAL
	try_again |= garbage_collect(); /* cleanup recursive lists/dicts */
#endif

	releasing = FALSE;
	if (!try_again)
	    break;
    }

    if (message && p == NULL)
	do_outofmem_msg(size);

theend:
#ifdef MEM_PROFILE
    mem_post_alloc((void **)&p, (size_t)size);
#endif
    return p;
}

#if defined(MEM_PROFILE) || defined(PROTO)
/*
 * realloc() with memory profiling.
 */
    void *
mem_realloc(ptr, size)
    void *ptr;
    size_t size;
{
    void *p;

    mem_pre_free(&ptr);
    mem_pre_alloc_s(&size);

    p = realloc(ptr, size);

    mem_post_alloc(&p, size);

    return p;
}
#endif

/*
* Avoid repeating the error message many times (they take 1 second each).
* Did_outofmem_msg is reset when a character is read.
*/
    void
do_outofmem_msg(size)
    long_u	size;
{
    if (!did_outofmem_msg)
    {
	/* Don't hide this message */
	emsg_silent = 0;

	/* Must come first to avoid coming back here when printing the error
	 * message fails, e.g. when setting v:errmsg. */
	did_outofmem_msg = TRUE;

	EMSGN(_("E342: Out of memory!  (allocating %lu bytes)"), size);
    }
}

#if defined(EXITFREE) || defined(PROTO)

# if defined(FEAT_SEARCHPATH)
static void free_findfile __ARGS((void));
# endif

/*
 * Free everything that we allocated.
 * Can be used to detect memory leaks, e.g., with ccmalloc.
 * NOTE: This is tricky!  Things are freed that functions depend on.  Don't be
 * surprised if Vim crashes...
 * Some things can't be freed, esp. things local to a library function.
 */
    void
free_all_mem()
{
    buf_T	*buf, *nextbuf;
    static int	entered = FALSE;

    /* When we cause a crash here it is caught and Vim tries to exit cleanly.
     * Don't try freeing everything again. */
    if (entered)
	return;
    entered = TRUE;

# ifdef FEAT_AUTOCMD
    block_autocmds();	    /* don't want to trigger autocommands here */
# endif

# ifdef FEAT_WINDOWS
    /* Close all tabs and windows.  Reset 'equalalways' to avoid redraws. */
    p_ea = FALSE;
    if (first_tabpage->tp_next != NULL)
	do_cmdline_cmd((char_u *)"tabonly!");
    if (firstwin != lastwin)
	do_cmdline_cmd((char_u *)"only!");
# endif

# if defined(FEAT_SPELL)
    /* Free all spell info. */
    spell_free_all();
# endif

# if defined(FEAT_USR_CMDS)
    /* Clear user commands (before deleting buffers). */
    ex_comclear(NULL);
# endif

# ifdef FEAT_MENU
    /* Clear menus. */
    do_cmdline_cmd((char_u *)"aunmenu *");
#  ifdef FEAT_MULTI_LANG
    do_cmdline_cmd((char_u *)"menutranslate clear");
#  endif
# endif

    /* Clear mappings, abbreviations, breakpoints. */
    do_cmdline_cmd((char_u *)"lmapclear");
    do_cmdline_cmd((char_u *)"xmapclear");
    do_cmdline_cmd((char_u *)"mapclear");
    do_cmdline_cmd((char_u *)"mapclear!");
    do_cmdline_cmd((char_u *)"abclear");
# if defined(FEAT_EVAL)
    do_cmdline_cmd((char_u *)"breakdel *");
# endif
# if defined(FEAT_PROFILE)
    do_cmdline_cmd((char_u *)"profdel *");
# endif
# if defined(FEAT_KEYMAP)
    do_cmdline_cmd((char_u *)"set keymap=");
#endif

# ifdef FEAT_TITLE
    free_titles();
# endif
# if defined(FEAT_SEARCHPATH)
    free_findfile();
# endif

    /* Obviously named calls. */
# if defined(FEAT_AUTOCMD)
    free_all_autocmds();
# endif
    clear_termcodes();
    free_all_options();
    free_all_marks();
    alist_clear(&global_alist);
    free_homedir();
    free_search_patterns();
    free_old_sub();
    free_last_insert();
    free_prev_shellcmd();
    free_regexp_stuff();
    free_tag_stuff();
    free_cd_dir();
# ifdef FEAT_SIGNS
    free_signs();
# endif
# ifdef FEAT_EVAL
    set_expr_line(NULL);
# endif
# ifdef FEAT_DIFF
    diff_clear(curtab);
# endif
    clear_sb_text();	      /* free any scrollback text */

    /* Free some global vars. */
    vim_free(username);
# ifdef FEAT_CLIPBOARD
    vim_free(clip_exclude_prog);
# endif
    vim_free(last_cmdline);
# ifdef FEAT_CMDHIST
    vim_free(new_last_cmdline);
# endif
    set_keep_msg(NULL, 0);
    vim_free(ff_expand_buffer);

    /* Clear cmdline history. */
    p_hi = 0;
# ifdef FEAT_CMDHIST
    init_history();
# endif

#ifdef FEAT_QUICKFIX
    {
	win_T	    *win;
	tabpage_T   *tab;

	qf_free_all(NULL);
	/* Free all location lists */
	FOR_ALL_TAB_WINDOWS(tab, win)
	    qf_free_all(win);
    }
#endif

    /* Close all script inputs. */
    close_all_scripts();

#if defined(FEAT_WINDOWS)
    /* Destroy all windows.  Must come before freeing buffers. */
    win_free_all();
#endif

    /* Free all buffers.  Reset 'autochdir' to avoid accessing things that
     * were freed already. */
#ifdef FEAT_AUTOCHDIR
    p_acd = FALSE;
#endif
    for (buf = firstbuf; buf != NULL; )
    {
	nextbuf = buf->b_next;
	close_buffer(NULL, buf, DOBUF_WIPE);
	if (buf_valid(buf))
	    buf = nextbuf;	/* didn't work, try next one */
	else
	    buf = firstbuf;
    }

#ifdef FEAT_ARABIC
    free_cmdline_buf();
#endif

    /* Clear registers. */
    clear_registers();
    ResetRedobuff();
    ResetRedobuff();

#if defined(FEAT_CLIENTSERVER) && defined(FEAT_X11)
    vim_free(serverDelayedStartName);
#endif

    /* highlight info */
    free_highlight();

    reset_last_sourcing();

#ifdef FEAT_WINDOWS
    free_tabpage(first_tabpage);
    first_tabpage = NULL;
#endif

# ifdef UNIX
    /* Machine-specific free. */
    mch_free_mem();
# endif

    /* message history */
    for (;;)
	if (delete_first_msg() == FAIL)
	    break;

# ifdef FEAT_EVAL
    eval_clear();
# endif

    free_termoptions();

    /* screenlines (can't display anything now!) */
    free_screenlines();

#if defined(USE_XSMP)
    xsmp_close();
#endif
#ifdef FEAT_GUI_GTK
    gui_mch_free_all();
#endif
    clear_hl_tables();

    vim_free(IObuff);
    vim_free(NameBuff);
}
#endif

/*
 * Copy "string" into newly allocated memory.
 */
    char_u *
vim_strsave(string)
    char_u	*string;
{
    char_u	*p;
    unsigned	len;

    len = (unsigned)STRLEN(string) + 1;
    p = alloc(len);
    if (p != NULL)
	mch_memmove(p, string, (size_t)len);
    return p;
}

/*
 * Copy up to "len" bytes of "string" into newly allocated memory and
 * terminate with a NUL.
 * The allocated memory always has size "len + 1", also when "string" is
 * shorter.
 */
    char_u *
vim_strnsave(string, len)
    char_u	*string;
    int		len;
{
    char_u	*p;

    p = alloc((unsigned)(len + 1));
    if (p != NULL)
    {
	STRNCPY(p, string, len);
	p[len] = NUL;
    }
    return p;
}

/*
 * Same as vim_strsave(), but any characters found in esc_chars are preceded
 * by a backslash.
 */
    char_u *
vim_strsave_escaped(string, esc_chars)
    char_u	*string;
    char_u	*esc_chars;
{
    return vim_strsave_escaped_ext(string, esc_chars, '\\', FALSE);
}

/*
 * Same as vim_strsave_escaped(), but when "bsl" is TRUE also escape
 * characters where rem_backslash() would remove the backslash.
 * Escape the characters with "cc".
 */
    char_u *
vim_strsave_escaped_ext(string, esc_chars, cc, bsl)
    char_u	*string;
    char_u	*esc_chars;
    int		cc;
    int		bsl;
{
    char_u	*p;
    char_u	*p2;
    char_u	*escaped_string;
    unsigned	length;
#ifdef FEAT_MBYTE
    int		l;
#endif

    /*
     * First count the number of backslashes required.
     * Then allocate the memory and insert them.
     */
    length = 1;				/* count the trailing NUL */
    for (p = string; *p; p++)
    {
#ifdef FEAT_MBYTE
	if (has_mbyte && (l = (*mb_ptr2len)(p)) > 1)
	{
	    length += l;		/* count a multibyte char */
	    p += l - 1;
	    continue;
	}
#endif
	if (vim_strchr(esc_chars, *p) != NULL || (bsl && rem_backslash(p)))
	    ++length;			/* count a backslash */
	++length;			/* count an ordinary char */
    }
    escaped_string = alloc(length);
    if (escaped_string != NULL)
    {
	p2 = escaped_string;
	for (p = string; *p; p++)
	{
#ifdef FEAT_MBYTE
	    if (has_mbyte && (l = (*mb_ptr2len)(p)) > 1)
	    {
		mch_memmove(p2, p, (size_t)l);
		p2 += l;
		p += l - 1;		/* skip multibyte char  */
		continue;
	    }
#endif
	    if (vim_strchr(esc_chars, *p) != NULL || (bsl && rem_backslash(p)))
		*p2++ = cc;
	    *p2++ = *p;
	}
	*p2 = NUL;
    }
    return escaped_string;
}

/*
 * Return TRUE when 'shell' has "csh" in the tail.
 */
    int
csh_like_shell()
{
    return (strstr((char *)gettail(p_sh), "csh") != NULL);
}

/*
 * Escape "string" for use as a shell argument with system().
 * This uses single quotes, except when we know we need to use double quotes
 * (MS-DOS and MS-Windows without 'shellslash' set).
 * Escape a newline, depending on the 'shell' option.
 * When "do_special" is TRUE also replace "!", "%", "#" and things starting
 * with "<" like "<cfile>".
 * Returns the result in allocated memory, NULL if we have run out.
 */
    char_u *
vim_strsave_shellescape(string, do_special)
    char_u	*string;
    int		do_special;
{
    unsigned	length;
    char_u	*p;
    char_u	*d;
    char_u	*escaped_string;
    int		l;
    int		csh_like;

    /* Only csh and similar shells expand '!' within single quotes.  For sh and
     * the like we must not put a backslash before it, it will be taken
     * literally.  If do_special is set the '!' will be escaped twice.
     * Csh also needs to have "\n" escaped twice when do_special is set. */
    csh_like = csh_like_shell();

    /* First count the number of extra bytes required. */
    length = (unsigned)STRLEN(string) + 3;  /* two quotes and a trailing NUL */
    for (p = string; *p != NUL; mb_ptr_adv(p))
    {
# if defined(WIN32) || defined(WIN16) || defined(DOS)
	if (!p_ssl)
	{
	    if (*p == '"')
		++length;		/* " -> "" */
	}
	else
# endif
	if (*p == '\'')
	    length += 3;		/* ' => '\'' */
	if (*p == '\n' || (*p == '!' && (csh_like || do_special)))
	{
	    ++length;			/* insert backslash */
	    if (csh_like && do_special)
		++length;		/* insert backslash */
	}
	if (do_special && find_cmdline_var(p, &l) >= 0)
	{
	    ++length;			/* insert backslash */
	    p += l - 1;
	}
    }

    /* Allocate memory for the result and fill it. */
    escaped_string = alloc(length);
    if (escaped_string != NULL)
    {
	d = escaped_string;

	/* add opening quote */
# if defined(WIN32) || defined(WIN16) || defined(DOS)
	if (!p_ssl)
	    *d++ = '"';
	else
# endif
	    *d++ = '\'';

	for (p = string; *p != NUL; )
	{
# if defined(WIN32) || defined(WIN16) || defined(DOS)
	    if (!p_ssl)
	    {
		if (*p == '"')
		{
		    *d++ = '"';
		    *d++ = '"';
		    ++p;
		    continue;
		}
	    }
	    else
# endif
	    if (*p == '\'')
	    {
		*d++ = '\'';
		*d++ = '\\';
		*d++ = '\'';
		*d++ = '\'';
		++p;
		continue;
	    }
	    if (*p == '\n' || (*p == '!' && (csh_like || do_special)))
	    {
		*d++ = '\\';
		if (csh_like && do_special)
		    *d++ = '\\';
		*d++ = *p++;
		continue;
	    }
	    if (do_special && find_cmdline_var(p, &l) >= 0)
	    {
		*d++ = '\\';		/* insert backslash */
		while (--l >= 0)	/* copy the var */
		    *d++ = *p++;
		continue;
	    }

	    MB_COPY_CHAR(p, d);
	}

	/* add terminating quote and finish with a NUL */
# if defined(WIN32) || defined(WIN16) || defined(DOS)
	if (!p_ssl)
	    *d++ = '"';
	else
# endif
	    *d++ = '\'';
	*d = NUL;
    }

    return escaped_string;
}

/*
 * Like vim_strsave(), but make all characters uppercase.
 * This uses ASCII lower-to-upper case translation, language independent.
 */
    char_u *
vim_strsave_up(string)
    char_u	*string;
{
    char_u *p1;

    p1 = vim_strsave(string);
    vim_strup(p1);
    return p1;
}

/*
 * Like vim_strnsave(), but make all characters uppercase.
 * This uses ASCII lower-to-upper case translation, language independent.
 */
    char_u *
vim_strnsave_up(string, len)
    char_u	*string;
    int		len;
{
    char_u *p1;

    p1 = vim_strnsave(string, len);
    vim_strup(p1);
    return p1;
}

/*
 * ASCII lower-to-upper case translation, language independent.
 */
    void
vim_strup(p)
    char_u	*p;
{
    char_u  *p2;
    int	    c;

    if (p != NULL)
    {
	p2 = p;
	while ((c = *p2) != NUL)
#ifdef EBCDIC
	    *p2++ = isalpha(c) ? toupper(c) : c;
#else
	    *p2++ = (c < 'a' || c > 'z') ? c : (c - 0x20);
#endif
    }
}

#if defined(FEAT_EVAL) || defined(FEAT_SPELL) || defined(PROTO)
/*
 * Make string "s" all upper-case and return it in allocated memory.
 * Handles multi-byte characters as well as possible.
 * Returns NULL when out of memory.
 */
    char_u *
strup_save(orig)
    char_u	*orig;
{
    char_u	*p;
    char_u	*res;

    res = p = vim_strsave(orig);

    if (res != NULL)
	while (*p != NUL)
	{
# ifdef FEAT_MBYTE
	    int		l;

	    if (enc_utf8)
	    {
		int	c, uc;
		int	newl;
		char_u	*s;

		c = utf_ptr2char(p);
		uc = utf_toupper(c);

		/* Reallocate string when byte count changes.  This is rare,
		 * thus it's OK to do another malloc()/free(). */
		l = utf_ptr2len(p);
		newl = utf_char2len(uc);
		if (newl != l)
		{
		    s = alloc((unsigned)STRLEN(res) + 1 + newl - l);
		    if (s == NULL)
			break;
		    mch_memmove(s, res, p - res);
		    STRCPY(s + (p - res) + newl, p + l);
		    p = s + (p - res);
		    vim_free(res);
		    res = s;
		}

		utf_char2bytes(uc, p);
		p += newl;
	    }
	    else if (has_mbyte && (l = (*mb_ptr2len)(p)) > 1)
		p += l;		/* skip multi-byte character */
	    else
# endif
	    {
		*p = TOUPPER_LOC(*p); /* note that toupper() can be a macro */
		p++;
	    }
	}

    return res;
}
#endif

/*
 * copy a space a number of times
 */
    void
copy_spaces(ptr, count)
    char_u	*ptr;
    size_t	count;
{
    size_t	i = count;
    char_u	*p = ptr;

    while (i--)
	*p++ = ' ';
}

#if defined(FEAT_VISUALEXTRA) || defined(PROTO)
/*
 * Copy a character a number of times.
 * Does not work for multi-byte characters!
 */
    void
copy_chars(ptr, count, c)
    char_u	*ptr;
    size_t	count;
    int		c;
{
    size_t	i = count;
    char_u	*p = ptr;

    while (i--)
	*p++ = c;
}
#endif

/*
 * delete spaces at the end of a string
 */
    void
del_trailing_spaces(ptr)
    char_u	*ptr;
{
    char_u	*q;

    q = ptr + STRLEN(ptr);
    while (--q > ptr && vim_iswhite(q[0]) && q[-1] != '\\' && q[-1] != Ctrl_V)
	*q = NUL;
}

/*
 * Like strncpy(), but always terminate the result with one NUL.
 * "to" must be "len + 1" long!
 */
    void
vim_strncpy(to, from, len)
    char_u	*to;
    char_u	*from;
    size_t	len;
{
    STRNCPY(to, from, len);
    to[len] = NUL;
}

/*
 * Like strcat(), but make sure the result fits in "tosize" bytes and is
 * always NUL terminated.
 */
    void
vim_strcat(to, from, tosize)
    char_u	*to;
    char_u	*from;
    size_t	tosize;
{
    size_t tolen = STRLEN(to);
    size_t fromlen = STRLEN(from);

    if (tolen + fromlen + 1 > tosize)
    {
	mch_memmove(to + tolen, from, tosize - tolen - 1);
	to[tosize - 1] = NUL;
    }
    else
	STRCPY(to + tolen, from);
}

/*
 * Isolate one part of a string option where parts are separated with
 * "sep_chars".
 * The part is copied into "buf[maxlen]".
 * "*option" is advanced to the next part.
 * The length is returned.
 */
    int
copy_option_part(option, buf, maxlen, sep_chars)
    char_u	**option;
    char_u	*buf;
    int		maxlen;
    char	*sep_chars;
{
    int	    len = 0;
    char_u  *p = *option;

    /* skip '.' at start of option part, for 'suffixes' */
    if (*p == '.')
	buf[len++] = *p++;
    while (*p != NUL && vim_strchr((char_u *)sep_chars, *p) == NULL)
    {
	/*
	 * Skip backslash before a separator character and space.
	 */
	if (p[0] == '\\' && vim_strchr((char_u *)sep_chars, p[1]) != NULL)
	    ++p;
	if (len < maxlen - 1)
	    buf[len++] = *p;
	++p;
    }
    buf[len] = NUL;

    if (*p != NUL && *p != ',')	/* skip non-standard separator */
	++p;
    p = skip_to_option_part(p);	/* p points to next file name */

    *option = p;
    return len;
}

/*
 * Replacement for free() that ignores NULL pointers.
 * Also skip free() when exiting for sure, this helps when we caught a deadly
 * signal that was caused by a crash in free().
 */
    void
vim_free(x)
    void *x;
{
    if (x != NULL && !really_exiting)
    {
#ifdef MEM_PROFILE
	mem_pre_free(&x);
#endif
	free(x);
    }
}

#ifndef HAVE_MEMSET
    void *
vim_memset(ptr, c, size)
    void    *ptr;
    int	    c;
    size_t  size;
{
    char *p = ptr;

    while (size-- > 0)
	*p++ = c;
    return ptr;
}
#endif

#ifdef VIM_MEMCMP
/*
 * Return zero when "b1" and "b2" are the same for "len" bytes.
 * Return non-zero otherwise.
 */
    int
vim_memcmp(b1, b2, len)
    void    *b1;
    void    *b2;
    size_t  len;
{
    char_u  *p1 = (char_u *)b1, *p2 = (char_u *)b2;

    for ( ; len > 0; --len)
    {
	if (*p1 != *p2)
	    return 1;
	++p1;
	++p2;
    }
    return 0;
}
#endif

#ifdef VIM_MEMMOVE
/*
 * Version of memmove() that handles overlapping source and destination.
 * For systems that don't have a function that is guaranteed to do that (SYSV).
 */
    void
mch_memmove(dst_arg, src_arg, len)
    void    *src_arg, *dst_arg;
    size_t  len;
{
    /*
     * A void doesn't have a size, we use char pointers.
     */
    char *dst = dst_arg, *src = src_arg;

					/* overlap, copy backwards */
    if (dst > src && dst < src + len)
    {
	src += len;
	dst += len;
	while (len-- > 0)
	    *--dst = *--src;
    }
    else				/* copy forwards */
	while (len-- > 0)
	    *dst++ = *src++;
}
#endif

#if (!defined(HAVE_STRCASECMP) && !defined(HAVE_STRICMP)) || defined(PROTO)
/*
 * Compare two strings, ignoring case, using current locale.
 * Doesn't work for multi-byte characters.
 * return 0 for match, < 0 for smaller, > 0 for bigger
 */
    int
vim_stricmp(s1, s2)
    char	*s1;
    char	*s2;
{
    int		i;

    for (;;)
    {
	i = (int)TOLOWER_LOC(*s1) - (int)TOLOWER_LOC(*s2);
	if (i != 0)
	    return i;			    /* this character different */
	if (*s1 == NUL)
	    break;			    /* strings match until NUL */
	++s1;
	++s2;
    }
    return 0;				    /* strings match */
}
#endif

#if (!defined(HAVE_STRNCASECMP) && !defined(HAVE_STRNICMP)) || defined(PROTO)
/*
 * Compare two strings, for length "len", ignoring case, using current locale.
 * Doesn't work for multi-byte characters.
 * return 0 for match, < 0 for smaller, > 0 for bigger
 */
    int
vim_strnicmp(s1, s2, len)
    char	*s1;
    char	*s2;
    size_t	len;
{
    int		i;

    while (len > 0)
    {
	i = (int)TOLOWER_LOC(*s1) - (int)TOLOWER_LOC(*s2);
	if (i != 0)
	    return i;			    /* this character different */
	if (*s1 == NUL)
	    break;			    /* strings match until NUL */
	++s1;
	++s2;
	--len;
    }
    return 0;				    /* strings match */
}
#endif

/*
 * Version of strchr() and strrchr() that handle unsigned char strings
 * with characters from 128 to 255 correctly.  It also doesn't return a
 * pointer to the NUL at the end of the string.
 */
    char_u  *
vim_strchr(string, c)
    char_u	*string;
    int		c;
{
    char_u	*p;
    int		b;

    p = string;
#ifdef FEAT_MBYTE
    if (enc_utf8 && c >= 0x80)
    {
	while (*p != NUL)
	{
	    if (utf_ptr2char(p) == c)
		return p;
	    p += (*mb_ptr2len)(p);
	}
	return NULL;
    }
    if (enc_dbcs != 0 && c > 255)
    {
	int	n2 = c & 0xff;

	c = ((unsigned)c >> 8) & 0xff;
	while ((b = *p) != NUL)
	{
	    if (b == c && p[1] == n2)
		return p;
	    p += (*mb_ptr2len)(p);
	}
	return NULL;
    }
    if (has_mbyte)
    {
	while ((b = *p) != NUL)
	{
	    if (b == c)
		return p;
	    p += (*mb_ptr2len)(p);
	}
	return NULL;
    }
#endif
    while ((b = *p) != NUL)
    {
	if (b == c)
	    return p;
	++p;
    }
    return NULL;
}

/*
 * Version of strchr() that only works for bytes and handles unsigned char
 * strings with characters above 128 correctly. It also doesn't return a
 * pointer to the NUL at the end of the string.
 */
    char_u  *
vim_strbyte(string, c)
    char_u	*string;
    int		c;
{
    char_u	*p = string;

    while (*p != NUL)
    {
	if (*p == c)
	    return p;
	++p;
    }
    return NULL;
}

/*
 * Search for last occurrence of "c" in "string".
 * Return NULL if not found.
 * Does not handle multi-byte char for "c"!
 */
    char_u  *
vim_strrchr(string, c)
    char_u	*string;
    int		c;
{
    char_u	*retval = NULL;
    char_u	*p = string;

    while (*p)
    {
	if (*p == c)
	    retval = p;
	mb_ptr_adv(p);
    }
    return retval;
}

/*
 * Vim's version of strpbrk(), in case it's missing.
 * Don't generate a prototype for this, causes problems when it's not used.
 */
#ifndef PROTO
# ifndef HAVE_STRPBRK
#  ifdef vim_strpbrk
#   undef vim_strpbrk
#  endif
    char_u *
vim_strpbrk(s, charset)
    char_u	*s;
    char_u	*charset;
{
    while (*s)
    {
	if (vim_strchr(charset, *s) != NULL)
	    return s;
	mb_ptr_adv(s);
    }
    return NULL;
}
# endif
#endif

/*
 * Vim has its own isspace() function, because on some machines isspace()
 * can't handle characters above 128.
 */
    int
vim_isspace(x)
    int	    x;
{
    return ((x >= 9 && x <= 13) || x == ' ');
}

/************************************************************************
 * Functions for handling growing arrays.
 */

/*
 * Clear an allocated growing array.
 */
    void
ga_clear(gap)
    garray_T *gap;
{
    vim_free(gap->ga_data);
    ga_init(gap);
}

/*
 * Clear a growing array that contains a list of strings.
 */
    void
ga_clear_strings(gap)
    garray_T *gap;
{
    int		i;

    for (i = 0; i < gap->ga_len; ++i)
	vim_free(((char_u **)(gap->ga_data))[i]);
    ga_clear(gap);
}

/*
 * Initialize a growing array.	Don't forget to set ga_itemsize and
 * ga_growsize!  Or use ga_init2().
 */
    void
ga_init(gap)
    garray_T *gap;
{
    gap->ga_data = NULL;
    gap->ga_maxlen = 0;
    gap->ga_len = 0;
}

    void
ga_init2(gap, itemsize, growsize)
    garray_T	*gap;
    int		itemsize;
    int		growsize;
{
    ga_init(gap);
    gap->ga_itemsize = itemsize;
    gap->ga_growsize = growsize;
}

/*
 * Make room in growing array "gap" for at least "n" items.
 * Return FAIL for failure, OK otherwise.
 */
    int
ga_grow(gap, n)
    garray_T	*gap;
    int		n;
{
    size_t	len;
    char_u	*pp;

    if (gap->ga_itemsize
	    && gap->ga_maxlen - gap->ga_len < n)
    {
	if (n < gap->ga_growsize)
	    n = gap->ga_growsize;
	len = gap->ga_itemsize * (gap->ga_len + n);
	pp = alloc_clear((unsigned)len);
	if (pp == NULL)
	    return FAIL;
	gap->ga_maxlen = gap->ga_len + n;
	if (gap->ga_data != NULL)
	{
	    mch_memmove(pp, gap->ga_data,
				      (size_t)(gap->ga_itemsize * gap->ga_len));
	    vim_free(gap->ga_data);
	}
	gap->ga_data = pp;
    }
    return OK;
}

/*
 * For a growing array that contains a list of strings: concatenate all the
 * strings with a separating comma.
 * Returns NULL when out of memory.
 */
    char_u *
ga_concat_strings(gap)
    garray_T *gap;
{
    int		i;
    int		len = 0;
    char_u	*s;

    for (i = 0; i < gap->ga_len; ++i)
	len += (int)STRLEN(((char_u **)(gap->ga_data))[i]) + 1;

    s = alloc(len + 1);
    if (s != NULL)
    {
	*s = NUL;
	for (i = 0; i < gap->ga_len; ++i)
	{
	    if (*s != NUL)
		STRCAT(s, ",");
	    STRCAT(s, ((char_u **)(gap->ga_data))[i]);
	}
    }
    return s;
}

/*
 * Concatenate a string to a growarray which contains characters.
 * Note: Does NOT copy the NUL at the end!
 */
    void
ga_concat(gap, s)
    garray_T	*gap;
    char_u	*s;
{
    int    len = (int)STRLEN(s);

    if (ga_grow(gap, len) == OK)
    {
	mch_memmove((char *)gap->ga_data + gap->ga_len, s, (size_t)len);
	gap->ga_len += len;
    }
}

/*
 * Append one byte to a growarray which contains bytes.
 */
    void
ga_append(gap, c)
    garray_T	*gap;
    int		c;
{
    if (ga_grow(gap, 1) == OK)
    {
	*((char *)gap->ga_data + gap->ga_len) = c;
	++gap->ga_len;
    }
}

#if (defined(UNIX) && !defined(USE_SYSTEM)) || defined(WIN3264)
/*
 * Append the text in "gap" below the cursor line and clear "gap".
 */
    void
append_ga_line(gap)
    garray_T	*gap;
{
    /* Remove trailing CR. */
    if (gap->ga_len > 0
	    && !curbuf->b_p_bin
	    && ((char_u *)gap->ga_data)[gap->ga_len - 1] == CAR)
	--gap->ga_len;
    ga_append(gap, NUL);
    ml_append(curwin->w_cursor.lnum++, gap->ga_data, 0, FALSE);
    gap->ga_len = 0;
}
#endif

/************************************************************************
 * functions that use lookup tables for various things, generally to do with
 * special key codes.
 */

/*
 * Some useful tables.
 */

static struct modmasktable
{
    short	mod_mask;	/* Bit-mask for particular key modifier */
    short	mod_flag;	/* Bit(s) for particular key modifier */
    char_u	name;		/* Single letter name of modifier */
} mod_mask_table[] =
{
    {MOD_MASK_ALT,		MOD_MASK_ALT,		(char_u)'M'},
    {MOD_MASK_META,		MOD_MASK_META,		(char_u)'T'},
    {MOD_MASK_CTRL,		MOD_MASK_CTRL,		(char_u)'C'},
    {MOD_MASK_SHIFT,		MOD_MASK_SHIFT,		(char_u)'S'},
    {MOD_MASK_MULTI_CLICK,	MOD_MASK_2CLICK,	(char_u)'2'},
    {MOD_MASK_MULTI_CLICK,	MOD_MASK_3CLICK,	(char_u)'3'},
    {MOD_MASK_MULTI_CLICK,	MOD_MASK_4CLICK,	(char_u)'4'},
#ifdef MACOS
    {MOD_MASK_CMD,		MOD_MASK_CMD,		(char_u)'D'},
#endif
    /* 'A' must be the last one */
    {MOD_MASK_ALT,		MOD_MASK_ALT,		(char_u)'A'},
    {0, 0, NUL}
};

/*
 * Shifted key terminal codes and their unshifted equivalent.
 * Don't add mouse codes here, they are handled separately!
 */
#define MOD_KEYS_ENTRY_SIZE 5

static char_u modifier_keys_table[] =
{
/*  mod mask	    with modifier		without modifier */
    MOD_MASK_SHIFT, '&', '9',			'@', '1',	/* begin */
    MOD_MASK_SHIFT, '&', '0',			'@', '2',	/* cancel */
    MOD_MASK_SHIFT, '*', '1',			'@', '4',	/* command */
    MOD_MASK_SHIFT, '*', '2',			'@', '5',	/* copy */
    MOD_MASK_SHIFT, '*', '3',			'@', '6',	/* create */
    MOD_MASK_SHIFT, '*', '4',			'k', 'D',	/* delete char */
    MOD_MASK_SHIFT, '*', '5',			'k', 'L',	/* delete line */
    MOD_MASK_SHIFT, '*', '7',			'@', '7',	/* end */
    MOD_MASK_CTRL,  KS_EXTRA, (int)KE_C_END,	'@', '7',	/* end */
    MOD_MASK_SHIFT, '*', '9',			'@', '9',	/* exit */
    MOD_MASK_SHIFT, '*', '0',			'@', '0',	/* find */
    MOD_MASK_SHIFT, '#', '1',			'%', '1',	/* help */
    MOD_MASK_SHIFT, '#', '2',			'k', 'h',	/* home */
    MOD_MASK_CTRL,  KS_EXTRA, (int)KE_C_HOME,	'k', 'h',	/* home */
    MOD_MASK_SHIFT, '#', '3',			'k', 'I',	/* insert */
    MOD_MASK_SHIFT, '#', '4',			'k', 'l',	/* left arrow */
    MOD_MASK_CTRL,  KS_EXTRA, (int)KE_C_LEFT,	'k', 'l',	/* left arrow */
    MOD_MASK_SHIFT, '%', 'a',			'%', '3',	/* message */
    MOD_MASK_SHIFT, '%', 'b',			'%', '4',	/* move */
    MOD_MASK_SHIFT, '%', 'c',			'%', '5',	/* next */
    MOD_MASK_SHIFT, '%', 'd',			'%', '7',	/* options */
    MOD_MASK_SHIFT, '%', 'e',			'%', '8',	/* previous */
    MOD_MASK_SHIFT, '%', 'f',			'%', '9',	/* print */
    MOD_MASK_SHIFT, '%', 'g',			'%', '0',	/* redo */
    MOD_MASK_SHIFT, '%', 'h',			'&', '3',	/* replace */
    MOD_MASK_SHIFT, '%', 'i',			'k', 'r',	/* right arr. */
    MOD_MASK_CTRL,  KS_EXTRA, (int)KE_C_RIGHT,	'k', 'r',	/* right arr. */
    MOD_MASK_SHIFT, '%', 'j',			'&', '5',	/* resume */
    MOD_MASK_SHIFT, '!', '1',			'&', '6',	/* save */
    MOD_MASK_SHIFT, '!', '2',			'&', '7',	/* suspend */
    MOD_MASK_SHIFT, '!', '3',			'&', '8',	/* undo */
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_UP,	'k', 'u',	/* up arrow */
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_DOWN,	'k', 'd',	/* down arrow */

								/* vt100 F1 */
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_XF1,	KS_EXTRA, (int)KE_XF1,
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_XF2,	KS_EXTRA, (int)KE_XF2,
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_XF3,	KS_EXTRA, (int)KE_XF3,
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_XF4,	KS_EXTRA, (int)KE_XF4,

    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F1,	'k', '1',	/* F1 */
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F2,	'k', '2',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F3,	'k', '3',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F4,	'k', '4',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F5,	'k', '5',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F6,	'k', '6',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F7,	'k', '7',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F8,	'k', '8',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F9,	'k', '9',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F10,	'k', ';',	/* F10 */

    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F11,	'F', '1',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F12,	'F', '2',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F13,	'F', '3',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F14,	'F', '4',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F15,	'F', '5',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F16,	'F', '6',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F17,	'F', '7',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F18,	'F', '8',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F19,	'F', '9',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F20,	'F', 'A',

    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F21,	'F', 'B',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F22,	'F', 'C',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F23,	'F', 'D',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F24,	'F', 'E',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F25,	'F', 'F',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F26,	'F', 'G',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F27,	'F', 'H',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F28,	'F', 'I',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F29,	'F', 'J',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F30,	'F', 'K',

    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F31,	'F', 'L',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F32,	'F', 'M',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F33,	'F', 'N',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F34,	'F', 'O',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F35,	'F', 'P',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F36,	'F', 'Q',
    MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F37,	'F', 'R',

							    /* TAB pseudo code*/
    MOD_MASK_SHIFT, 'k', 'B',			KS_EXTRA, (int)KE_TAB,

    NUL
};

static struct key_name_entry
{
    int	    key;	/* Special key code or ascii value */
    char_u  *name;	/* Name of key */
} key_names_table[] =
{
    {' ',		(char_u *)"Space"},
    {TAB,		(char_u *)"Tab"},
    {K_TAB,		(char_u *)"Tab"},
    {NL,		(char_u *)"NL"},
    {NL,		(char_u *)"NewLine"},	/* Alternative name */
    {NL,		(char_u *)"LineFeed"},	/* Alternative name */
    {NL,		(char_u *)"LF"},	/* Alternative name */
    {CAR,		(char_u *)"CR"},
    {CAR,		(char_u *)"Return"},	/* Alternative name */
    {CAR,		(char_u *)"Enter"},	/* Alternative name */
    {K_BS,		(char_u *)"BS"},
    {K_BS,		(char_u *)"BackSpace"},	/* Alternative name */
    {ESC,		(char_u *)"Esc"},
    {CSI,		(char_u *)"CSI"},
    {K_CSI,		(char_u *)"xCSI"},
    {'|',		(char_u *)"Bar"},
    {'\\',		(char_u *)"Bslash"},
    {K_DEL,		(char_u *)"Del"},
    {K_DEL,		(char_u *)"Delete"},	/* Alternative name */
    {K_KDEL,		(char_u *)"kDel"},
    {K_UP,		(char_u *)"Up"},
    {K_DOWN,		(char_u *)"Down"},
    {K_LEFT,		(char_u *)"Left"},
    {K_RIGHT,		(char_u *)"Right"},
    {K_XUP,		(char_u *)"xUp"},
    {K_XDOWN,		(char_u *)"xDown"},
    {K_XLEFT,		(char_u *)"xLeft"},
    {K_XRIGHT,		(char_u *)"xRight"},

    {K_F1,		(char_u *)"F1"},
    {K_F2,		(char_u *)"F2"},
    {K_F3,		(char_u *)"F3"},
    {K_F4,		(char_u *)"F4"},
    {K_F5,		(char_u *)"F5"},
    {K_F6,		(char_u *)"F6"},
    {K_F7,		(char_u *)"F7"},
    {K_F8,		(char_u *)"F8"},
    {K_F9,		(char_u *)"F9"},
    {K_F10,		(char_u *)"F10"},

    {K_F11,		(char_u *)"F11"},
    {K_F12,		(char_u *)"F12"},
    {K_F13,		(char_u *)"F13"},
    {K_F14,		(char_u *)"F14"},
    {K_F15,		(char_u *)"F15"},
    {K_F16,		(char_u *)"F16"},
    {K_F17,		(char_u *)"F17"},
    {K_F18,		(char_u *)"F18"},
    {K_F19,		(char_u *)"F19"},
    {K_F20,		(char_u *)"F20"},

    {K_F21,		(char_u *)"F21"},
    {K_F22,		(char_u *)"F22"},
    {K_F23,		(char_u *)"F23"},
    {K_F24,		(char_u *)"F24"},
    {K_F25,		(char_u *)"F25"},
    {K_F26,		(char_u *)"F26"},
    {K_F27,		(char_u *)"F27"},
    {K_F28,		(char_u *)"F28"},
    {K_F29,		(char_u *)"F29"},
    {K_F30,		(char_u *)"F30"},

    {K_F31,		(char_u *)"F31"},
    {K_F32,		(char_u *)"F32"},
    {K_F33,		(char_u *)"F33"},
    {K_F34,		(char_u *)"F34"},
    {K_F35,		(char_u *)"F35"},
    {K_F36,		(char_u *)"F36"},
    {K_F37,		(char_u *)"F37"},

    {K_XF1,		(char_u *)"xF1"},
    {K_XF2,		(char_u *)"xF2"},
    {K_XF3,		(char_u *)"xF3"},
    {K_XF4,		(char_u *)"xF4"},

    {K_HELP,		(char_u *)"Help"},
    {K_UNDO,		(char_u *)"Undo"},
    {K_INS,		(char_u *)"Insert"},
    {K_INS,		(char_u *)"Ins"},	/* Alternative name */
    {K_KINS,		(char_u *)"kInsert"},
    {K_HOME,		(char_u *)"Home"},
    {K_KHOME,		(char_u *)"kHome"},
    {K_XHOME,		(char_u *)"xHome"},
    {K_ZHOME,		(char_u *)"zHome"},
    {K_END,		(char_u *)"End"},
    {K_KEND,		(char_u *)"kEnd"},
    {K_XEND,		(char_u *)"xEnd"},
    {K_ZEND,		(char_u *)"zEnd"},
    {K_PAGEUP,		(char_u *)"PageUp"},
    {K_PAGEDOWN,	(char_u *)"PageDown"},
    {K_KPAGEUP,		(char_u *)"kPageUp"},
    {K_KPAGEDOWN,	(char_u *)"kPageDown"},

    {K_KPLUS,		(char_u *)"kPlus"},
    {K_KMINUS,		(char_u *)"kMinus"},
    {K_KDIVIDE,		(char_u *)"kDivide"},
    {K_KMULTIPLY,	(char_u *)"kMultiply"},
    {K_KENTER,		(char_u *)"kEnter"},
    {K_KPOINT,		(char_u *)"kPoint"},

    {K_K0,		(char_u *)"k0"},
    {K_K1,		(char_u *)"k1"},
    {K_K2,		(char_u *)"k2"},
    {K_K3,		(char_u *)"k3"},
    {K_K4,		(char_u *)"k4"},
    {K_K5,		(char_u *)"k5"},
    {K_K6,		(char_u *)"k6"},
    {K_K7,		(char_u *)"k7"},
    {K_K8,		(char_u *)"k8"},
    {K_K9,		(char_u *)"k9"},

    {'<',		(char_u *)"lt"},

    {K_MOUSE,		(char_u *)"Mouse"},
    {K_NETTERM_MOUSE,	(char_u *)"NetMouse"},
    {K_DEC_MOUSE,	(char_u *)"DecMouse"},
    {K_JSBTERM_MOUSE,	(char_u *)"JsbMouse"},
    {K_PTERM_MOUSE,	(char_u *)"PtermMouse"},
    {K_LEFTMOUSE,	(char_u *)"LeftMouse"},
    {K_LEFTMOUSE_NM,	(char_u *)"LeftMouseNM"},
    {K_LEFTDRAG,	(char_u *)"LeftDrag"},
    {K_LEFTRELEASE,	(char_u *)"LeftRelease"},
    {K_LEFTRELEASE_NM,	(char_u *)"LeftReleaseNM"},
    {K_MIDDLEMOUSE,	(char_u *)"MiddleMouse"},
    {K_MIDDLEDRAG,	(char_u *)"MiddleDrag"},
    {K_MIDDLERELEASE,	(char_u *)"MiddleRelease"},
    {K_RIGHTMOUSE,	(char_u *)"RightMouse"},
    {K_RIGHTDRAG,	(char_u *)"RightDrag"},
    {K_RIGHTRELEASE,	(char_u *)"RightRelease"},
    {K_MOUSEDOWN,	(char_u *)"ScrollWheelUp"},
    {K_MOUSEUP,		(char_u *)"ScrollWheelDown"},
    {K_MOUSELEFT,	(char_u *)"ScrollWheelRight"},
    {K_MOUSERIGHT,	(char_u *)"ScrollWheelLeft"},
    {K_MOUSEDOWN,	(char_u *)"MouseDown"}, /* OBSOLETE: Use	  */
    {K_MOUSEUP,		(char_u *)"MouseUp"},	/* ScrollWheelXXX instead */
    {K_X1MOUSE,		(char_u *)"X1Mouse"},
    {K_X1DRAG,		(char_u *)"X1Drag"},
    {K_X1RELEASE,		(char_u *)"X1Release"},
    {K_X2MOUSE,		(char_u *)"X2Mouse"},
    {K_X2DRAG,		(char_u *)"X2Drag"},
    {K_X2RELEASE,		(char_u *)"X2Release"},
    {K_DROP,		(char_u *)"Drop"},
    {K_ZERO,		(char_u *)"Nul"},
#ifdef FEAT_EVAL
    {K_SNR,		(char_u *)"SNR"},
#endif
    {K_PLUG,		(char_u *)"Plug"},
    {0,			NULL}
};

#define KEY_NAMES_TABLE_LEN (sizeof(key_names_table) / sizeof(struct key_name_entry))

#ifdef FEAT_MOUSE
static struct mousetable
{
    int	    pseudo_code;	/* Code for pseudo mouse event */
    int	    button;		/* Which mouse button is it? */
    int	    is_click;		/* Is it a mouse button click event? */
    int	    is_drag;		/* Is it a mouse drag event? */
} mouse_table[] =
{
    {(int)KE_LEFTMOUSE,		MOUSE_LEFT,	TRUE,	FALSE},
#ifdef FEAT_GUI
    {(int)KE_LEFTMOUSE_NM,	MOUSE_LEFT,	TRUE,	FALSE},
#endif
    {(int)KE_LEFTDRAG,		MOUSE_LEFT,	FALSE,	TRUE},
    {(int)KE_LEFTRELEASE,	MOUSE_LEFT,	FALSE,	FALSE},
#ifdef FEAT_GUI
    {(int)KE_LEFTRELEASE_NM,	MOUSE_LEFT,	FALSE,	FALSE},
#endif
    {(int)KE_MIDDLEMOUSE,	MOUSE_MIDDLE,	TRUE,	FALSE},
    {(int)KE_MIDDLEDRAG,	MOUSE_MIDDLE,	FALSE,	TRUE},
    {(int)KE_MIDDLERELEASE,	MOUSE_MIDDLE,	FALSE,	FALSE},
    {(int)KE_RIGHTMOUSE,	MOUSE_RIGHT,	TRUE,	FALSE},
    {(int)KE_RIGHTDRAG,		MOUSE_RIGHT,	FALSE,	TRUE},
    {(int)KE_RIGHTRELEASE,	MOUSE_RIGHT,	FALSE,	FALSE},
    {(int)KE_X1MOUSE,		MOUSE_X1,	TRUE,	FALSE},
    {(int)KE_X1DRAG,		MOUSE_X1,	FALSE,	TRUE},
    {(int)KE_X1RELEASE,		MOUSE_X1,	FALSE,	FALSE},
    {(int)KE_X2MOUSE,		MOUSE_X2,	TRUE,	FALSE},
    {(int)KE_X2DRAG,		MOUSE_X2,	FALSE,	TRUE},
    {(int)KE_X2RELEASE,		MOUSE_X2,	FALSE,	FALSE},
    /* DRAG without CLICK */
    {(int)KE_IGNORE,		MOUSE_RELEASE,	FALSE,	TRUE},
    /* RELEASE without CLICK */
    {(int)KE_IGNORE,		MOUSE_RELEASE,	FALSE,	FALSE},
    {0,				0,		0,	0},
};
#endif /* FEAT_MOUSE */

/*
 * Return the modifier mask bit (MOD_MASK_*) which corresponds to the given
 * modifier name ('S' for Shift, 'C' for Ctrl etc).
 */
    int
name_to_mod_mask(c)
    int	    c;
{
    int	    i;

    c = TOUPPER_ASC(c);
    for (i = 0; mod_mask_table[i].mod_mask != 0; i++)
	if (c == mod_mask_table[i].name)
	    return mod_mask_table[i].mod_flag;
    return 0;
}

/*
 * Check if if there is a special key code for "key" that includes the
 * modifiers specified.
 */
    int
simplify_key(key, modifiers)
    int	    key;
    int	    *modifiers;
{
    int	    i;
    int	    key0;
    int	    key1;

    if (*modifiers & (MOD_MASK_SHIFT | MOD_MASK_CTRL | MOD_MASK_ALT))
    {
	/* TAB is a special case */
	if (key == TAB && (*modifiers & MOD_MASK_SHIFT))
	{
	    *modifiers &= ~MOD_MASK_SHIFT;
	    return K_S_TAB;
	}
	key0 = KEY2TERMCAP0(key);
	key1 = KEY2TERMCAP1(key);
	for (i = 0; modifier_keys_table[i] != NUL; i += MOD_KEYS_ENTRY_SIZE)
	    if (key0 == modifier_keys_table[i + 3]
		    && key1 == modifier_keys_table[i + 4]
		    && (*modifiers & modifier_keys_table[i]))
	    {
		*modifiers &= ~modifier_keys_table[i];
		return TERMCAP2KEY(modifier_keys_table[i + 1],
						   modifier_keys_table[i + 2]);
	    }
    }
    return key;
}

/*
 * Change <xHome> to <Home>, <xUp> to <Up>, etc.
 */
    int
handle_x_keys(key)
    int	    key;
{
    switch (key)
    {
	case K_XUP:	return K_UP;
	case K_XDOWN:	return K_DOWN;
	case K_XLEFT:	return K_LEFT;
	case K_XRIGHT:	return K_RIGHT;
	case K_XHOME:	return K_HOME;
	case K_ZHOME:	return K_HOME;
	case K_XEND:	return K_END;
	case K_ZEND:	return K_END;
	case K_XF1:	return K_F1;
	case K_XF2:	return K_F2;
	case K_XF3:	return K_F3;
	case K_XF4:	return K_F4;
	case K_S_XF1:	return K_S_F1;
	case K_S_XF2:	return K_S_F2;
	case K_S_XF3:	return K_S_F3;
	case K_S_XF4:	return K_S_F4;
    }
    return key;
}

/*
 * Return a string which contains the name of the given key when the given
 * modifiers are down.
 */
    char_u *
get_special_key_name(c, modifiers)
    int	    c;
    int	    modifiers;
{
    static char_u string[MAX_KEY_NAME_LEN + 1];

    int	    i, idx;
    int	    table_idx;
    char_u  *s;

    string[0] = '<';
    idx = 1;

    /* Key that stands for a normal character. */
    if (IS_SPECIAL(c) && KEY2TERMCAP0(c) == KS_KEY)
	c = KEY2TERMCAP1(c);

    /*
     * Translate shifted special keys into unshifted keys and set modifier.
     * Same for CTRL and ALT modifiers.
     */
    if (IS_SPECIAL(c))
    {
	for (i = 0; modifier_keys_table[i] != 0; i += MOD_KEYS_ENTRY_SIZE)
	    if (       KEY2TERMCAP0(c) == (int)modifier_keys_table[i + 1]
		    && (int)KEY2TERMCAP1(c) == (int)modifier_keys_table[i + 2])
	    {
		modifiers |= modifier_keys_table[i];
		c = TERMCAP2KEY(modifier_keys_table[i + 3],
						   modifier_keys_table[i + 4]);
		break;
	    }
    }

    /* try to find the key in the special key table */
    table_idx = find_special_key_in_table(c);

    /*
     * When not a known special key, and not a printable character, try to
     * extract modifiers.
     */
    if (c > 0
#ifdef FEAT_MBYTE
	    && (*mb_char2len)(c) == 1
#endif
       )
    {
	if (table_idx < 0
		&& (!vim_isprintc(c) || (c & 0x7f) == ' ')
		&& (c & 0x80))
	{
	    c &= 0x7f;
	    modifiers |= MOD_MASK_ALT;
	    /* try again, to find the un-alted key in the special key table */
	    table_idx = find_special_key_in_table(c);
	}
	if (table_idx < 0 && !vim_isprintc(c) && c < ' ')
	{
#ifdef EBCDIC
	    c = CtrlChar(c);
#else
	    c += '@';
#endif
	    modifiers |= MOD_MASK_CTRL;
	}
    }

    /* translate the modifier into a string */
    for (i = 0; mod_mask_table[i].name != 'A'; i++)
	if ((modifiers & mod_mask_table[i].mod_mask)
						== mod_mask_table[i].mod_flag)
	{
	    string[idx++] = mod_mask_table[i].name;
	    string[idx++] = (char_u)'-';
	}

    if (table_idx < 0)		/* unknown special key, may output t_xx */
    {
	if (IS_SPECIAL(c))
	{
	    string[idx++] = 't';
	    string[idx++] = '_';
	    string[idx++] = KEY2TERMCAP0(c);
	    string[idx++] = KEY2TERMCAP1(c);
	}
	/* Not a special key, only modifiers, output directly */
	else
	{
#ifdef FEAT_MBYTE
	    if (has_mbyte && (*mb_char2len)(c) > 1)
		idx += (*mb_char2bytes)(c, string + idx);
	    else
#endif
	    if (vim_isprintc(c))
		string[idx++] = c;
	    else
	    {
		s = transchar(c);
		while (*s)
		    string[idx++] = *s++;
	    }
	}
    }
    else		/* use name of special key */
    {
	STRCPY(string + idx, key_names_table[table_idx].name);
	idx = (int)STRLEN(string);
    }
    string[idx++] = '>';
    string[idx] = NUL;
    return string;
}

/*
 * Try translating a <> name at (*srcp)[] to dst[].
 * Return the number of characters added to dst[], zero for no match.
 * If there is a match, srcp is advanced to after the <> name.
 * dst[] must be big enough to hold the result (up to six characters)!
 */
    int
trans_special(srcp, dst, keycode)
    char_u	**srcp;
    char_u	*dst;
    int		keycode; /* prefer key code, e.g. K_DEL instead of DEL */
{
    int		modifiers = 0;
    int		key;
    int		dlen = 0;

    key = find_special_key(srcp, &modifiers, keycode, FALSE);
    if (key == 0)
	return 0;

    /* Put the appropriate modifier in a string */
    if (modifiers != 0)
    {
	dst[dlen++] = K_SPECIAL;
	dst[dlen++] = KS_MODIFIER;
	dst[dlen++] = modifiers;
    }

    if (IS_SPECIAL(key))
    {
	dst[dlen++] = K_SPECIAL;
	dst[dlen++] = KEY2TERMCAP0(key);
	dst[dlen++] = KEY2TERMCAP1(key);
    }
#ifdef FEAT_MBYTE
    else if (has_mbyte && !keycode)
	dlen += (*mb_char2bytes)(key, dst + dlen);
#endif
    else if (keycode)
	dlen = (int)(add_char2buf(key, dst + dlen) - dst);
    else
	dst[dlen++] = key;

    return dlen;
}

/*
 * Try translating a <> name at (*srcp)[], return the key and modifiers.
 * srcp is advanced to after the <> name.
 * returns 0 if there is no match.
 */
    int
find_special_key(srcp, modp, keycode, keep_x_key)
    char_u	**srcp;
    int		*modp;
    int		keycode;     /* prefer key code, e.g. K_DEL instead of DEL */
    int		keep_x_key;  /* don't translate xHome to Home key */
{
    char_u	*last_dash;
    char_u	*end_of_name;
    char_u	*src;
    char_u	*bp;
    int		modifiers;
    int		bit;
    int		key;
    unsigned long n;
    int		l;

    src = *srcp;
    if (src[0] != '<')
	return 0;

    /* Find end of modifier list */
    last_dash = src;
    for (bp = src + 1; *bp == '-' || vim_isIDc(*bp); bp++)
    {
	if (*bp == '-')
	{
	    last_dash = bp;
	    if (bp[1] != NUL)
	    {
#ifdef FEAT_MBYTE
		if (has_mbyte)
		    l = mb_ptr2len(bp + 1);
		else
#endif
		    l = 1;
		if (bp[l + 1] == '>')
		    bp += l;	/* anything accepted, like <C-?> */
	    }
	}
	if (bp[0] == 't' && bp[1] == '_' && bp[2] && bp[3])
	    bp += 3;	/* skip t_xx, xx may be '-' or '>' */
	else if (STRNICMP(bp, "char-", 5) == 0)
	{
	    vim_str2nr(bp + 5, NULL, &l, TRUE, TRUE, NULL, NULL);
	    bp += l + 5;
	    break;
	}
    }

    if (*bp == '>')	/* found matching '>' */
    {
	end_of_name = bp + 1;

	/* Which modifiers are given? */
	modifiers = 0x0;
	for (bp = src + 1; bp < last_dash; bp++)
	{
	    if (*bp != '-')
	    {
		bit = name_to_mod_mask(*bp);
		if (bit == 0x0)
		    break;	/* Illegal modifier name */
		modifiers |= bit;
	    }
	}

	/*
	 * Legal modifier name.
	 */
	if (bp >= last_dash)
	{
	    if (STRNICMP(last_dash + 1, "char-", 5) == 0
						 && VIM_ISDIGIT(last_dash[6]))
	    {
		/* <Char-123> or <Char-033> or <Char-0x33> */
		vim_str2nr(last_dash + 6, NULL, NULL, TRUE, TRUE, NULL, &n);
		key = (int)n;
	    }
	    else
	    {
		/*
		 * Modifier with single letter, or special key name.
		 */
#ifdef FEAT_MBYTE
		if (has_mbyte)
		    l = mb_ptr2len(last_dash + 1);
		else
#endif
		    l = 1;
		if (modifiers != 0 && last_dash[l + 1] == '>')
		    key = PTR2CHAR(last_dash + 1);
		else
		{
		    key = get_special_key_code(last_dash + 1);
		    if (!keep_x_key)
			key = handle_x_keys(key);
		}
	    }

	    /*
	     * get_special_key_code() may return NUL for invalid
	     * special key name.
	     */
	    if (key != NUL)
	    {
		/*
		 * Only use a modifier when there is no special key code that
		 * includes the modifier.
		 */
		key = simplify_key(key, &modifiers);

		if (!keycode)
		{
		    /* don't want keycode, use single byte code */
		    if (key == K_BS)
			key = BS;
		    else if (key == K_DEL || key == K_KDEL)
			key = DEL;
		}

		/*
		 * Normal Key with modifier: Try to make a single byte code.
		 */
		if (!IS_SPECIAL(key))
		    key = extract_modifiers(key, &modifiers);

		*modp = modifiers;
		*srcp = end_of_name;
		return key;
	    }
	}
    }
    return 0;
}

/*
 * Try to include modifiers in the key.
 * Changes "Shift-a" to 'A', "Alt-A" to 0xc0, etc.
 */
    int
extract_modifiers(key, modp)
    int	    key;
    int	    *modp;
{
    int	modifiers = *modp;

#ifdef MACOS
    /* Command-key really special, No fancynest */
    if (!(modifiers & MOD_MASK_CMD))
#endif
    if ((modifiers & MOD_MASK_SHIFT) && ASCII_ISALPHA(key))
    {
	key = TOUPPER_ASC(key);
	modifiers &= ~MOD_MASK_SHIFT;
    }
    if ((modifiers & MOD_MASK_CTRL)
#ifdef EBCDIC
	    /* * TODO: EBCDIC Better use:
	     * && (Ctrl_chr(key) || key == '?')
	     * ???  */
	    && strchr("?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_", key)
						       != NULL
#else
	    && ((key >= '?' && key <= '_') || ASCII_ISALPHA(key))
#endif
	    )
    {
	key = Ctrl_chr(key);
	modifiers &= ~MOD_MASK_CTRL;
	/* <C-@> is <Nul> */
	if (key == 0)
	    key = K_ZERO;
    }
#ifdef MACOS
    /* Command-key really special, No fancynest */
    if (!(modifiers & MOD_MASK_CMD))
#endif
    if ((modifiers & MOD_MASK_ALT) && key < 0x80
#ifdef FEAT_MBYTE
	    && !enc_dbcs		/* avoid creating a lead byte */
#endif
	    )
    {
	key |= 0x80;
	modifiers &= ~MOD_MASK_ALT;	/* remove the META modifier */
    }

    *modp = modifiers;
    return key;
}

/*
 * Try to find key "c" in the special key table.
 * Return the index when found, -1 when not found.
 */
    int
find_special_key_in_table(c)
    int	    c;
{
    int	    i;

    for (i = 0; key_names_table[i].name != NULL; i++)
	if (c == key_names_table[i].key)
	    break;
    if (key_names_table[i].name == NULL)
	i = -1;
    return i;
}

/*
 * Find the special key with the given name (the given string does not have to
 * end with NUL, the name is assumed to end before the first non-idchar).
 * If the name starts with "t_" the next two characters are interpreted as a
 * termcap name.
 * Return the key code, or 0 if not found.
 */
    int
get_special_key_code(name)
    char_u  *name;
{
    char_u  *table_name;
    char_u  string[3];
    int	    i, j;

    /*
     * If it's <t_xx> we get the code for xx from the termcap
     */
    if (name[0] == 't' && name[1] == '_' && name[2] != NUL && name[3] != NUL)
    {
	string[0] = name[2];
	string[1] = name[3];
	string[2] = NUL;
	if (add_termcap_entry(string, FALSE) == OK)
	    return TERMCAP2KEY(name[2], name[3]);
    }
    else
	for (i = 0; key_names_table[i].name != NULL; i++)
	{
	    table_name = key_names_table[i].name;
	    for (j = 0; vim_isIDc(name[j]) && table_name[j] != NUL; j++)
		if (TOLOWER_ASC(table_name[j]) != TOLOWER_ASC(name[j]))
		    break;
	    if (!vim_isIDc(name[j]) && table_name[j] == NUL)
		return key_names_table[i].key;
	}
    return 0;
}

#if defined(FEAT_CMDL_COMPL) || defined(PROTO)
    char_u *
get_key_name(i)
    int	    i;
{
    if (i >= (int)KEY_NAMES_TABLE_LEN)
	return NULL;
    return  key_names_table[i].name;
}
#endif

#if defined(FEAT_MOUSE) || defined(PROTO)
/*
 * Look up the given mouse code to return the relevant information in the other
 * arguments.  Return which button is down or was released.
 */
    int
get_mouse_button(code, is_click, is_drag)
    int	    code;
    int	    *is_click;
    int	    *is_drag;
{
    int	    i;

    for (i = 0; mouse_table[i].pseudo_code; i++)
	if (code == mouse_table[i].pseudo_code)
	{
	    *is_click = mouse_table[i].is_click;
	    *is_drag = mouse_table[i].is_drag;
	    return mouse_table[i].button;
	}
    return 0;	    /* Shouldn't get here */
}

/*
 * Return the appropriate pseudo mouse event token (KE_LEFTMOUSE etc) based on
 * the given information about which mouse button is down, and whether the
 * mouse was clicked, dragged or released.
 */
    int
get_pseudo_mouse_code(button, is_click, is_drag)
    int	    button;	/* eg MOUSE_LEFT */
    int	    is_click;
    int	    is_drag;
{
    int	    i;

    for (i = 0; mouse_table[i].pseudo_code; i++)
	if (button == mouse_table[i].button
	    && is_click == mouse_table[i].is_click
	    && is_drag == mouse_table[i].is_drag)
	{
#ifdef FEAT_GUI
	    /* Trick: a non mappable left click and release has mouse_col -1
	     * or added MOUSE_COLOFF.  Used for 'mousefocus' in
	     * gui_mouse_moved() */
	    if (mouse_col < 0 || mouse_col > MOUSE_COLOFF)
	    {
		if (mouse_col < 0)
		    mouse_col = 0;
		else
		    mouse_col -= MOUSE_COLOFF;
		if (mouse_table[i].pseudo_code == (int)KE_LEFTMOUSE)
		    return (int)KE_LEFTMOUSE_NM;
		if (mouse_table[i].pseudo_code == (int)KE_LEFTRELEASE)
		    return (int)KE_LEFTRELEASE_NM;
	    }
#endif
	    return mouse_table[i].pseudo_code;
	}
    return (int)KE_IGNORE;	    /* not recognized, ignore it */
}
#endif /* FEAT_MOUSE */

/*
 * Return the current end-of-line type: EOL_DOS, EOL_UNIX or EOL_MAC.
 */
    int
get_fileformat(buf)
    buf_T	*buf;
{
    int		c = *buf->b_p_ff;

    if (buf->b_p_bin || c == 'u')
	return EOL_UNIX;
    if (c == 'm')
	return EOL_MAC;
    return EOL_DOS;
}

/*
 * Like get_fileformat(), but override 'fileformat' with "p" for "++opt=val"
 * argument.
 */
    int
get_fileformat_force(buf, eap)
    buf_T	*buf;
    exarg_T	*eap;	    /* can be NULL! */
{
    int		c;

    if (eap != NULL && eap->force_ff != 0)
	c = eap->cmd[eap->force_ff];
    else
    {
	if ((eap != NULL && eap->force_bin != 0)
			       ? (eap->force_bin == FORCE_BIN) : buf->b_p_bin)
	    return EOL_UNIX;
	c = *buf->b_p_ff;
    }
    if (c == 'u')
	return EOL_UNIX;
    if (c == 'm')
	return EOL_MAC;
    return EOL_DOS;
}

/*
 * Set the current end-of-line type to EOL_DOS, EOL_UNIX or EOL_MAC.
 * Sets both 'textmode' and 'fileformat'.
 * Note: Does _not_ set global value of 'textmode'!
 */
    void
set_fileformat(t, opt_flags)
    int		t;
    int		opt_flags;	/* OPT_LOCAL and/or OPT_GLOBAL */
{
    char	*p = NULL;

    switch (t)
    {
    case EOL_DOS:
	p = FF_DOS;
	curbuf->b_p_tx = TRUE;
	break;
    case EOL_UNIX:
	p = FF_UNIX;
	curbuf->b_p_tx = FALSE;
	break;
    case EOL_MAC:
	p = FF_MAC;
	curbuf->b_p_tx = FALSE;
	break;
    }
    if (p != NULL)
	set_string_option_direct((char_u *)"ff", -1, (char_u *)p,
						     OPT_FREE | opt_flags, 0);

#ifdef FEAT_WINDOWS
    /* This may cause the buffer to become (un)modified. */
    check_status(curbuf);
    redraw_tabline = TRUE;
#endif
#ifdef FEAT_TITLE
    need_maketitle = TRUE;	    /* set window title later */
#endif
}

/*
 * Return the default fileformat from 'fileformats'.
 */
    int
default_fileformat()
{
    switch (*p_ffs)
    {
	case 'm':   return EOL_MAC;
	case 'd':   return EOL_DOS;
    }
    return EOL_UNIX;
}

/*
 * Call shell.	Calls mch_call_shell, with 'shellxquote' added.
 */
    int
call_shell(cmd, opt)
    char_u	*cmd;
    int		opt;
{
    char_u	*ncmd;
    int		retval;
#ifdef FEAT_PROFILE
    proftime_T	wait_time;
#endif

    if (p_verbose > 3)
    {
	verbose_enter();
	smsg((char_u *)_("Calling shell to execute: \"%s\""),
						    cmd == NULL ? p_sh : cmd);
	out_char('\n');
	cursor_on();
	verbose_leave();
    }

#ifdef FEAT_PROFILE
    if (do_profiling == PROF_YES)
	prof_child_enter(&wait_time);
#endif

    if (*p_sh == NUL)
    {
	EMSG(_(e_shellempty));
	retval = -1;
    }
    else
    {
#ifdef FEAT_GUI_MSWIN
	/* Don't hide the pointer while executing a shell command. */
	gui_mch_mousehide(FALSE);
#endif
#ifdef FEAT_GUI
	++hold_gui_events;
#endif
	/* The external command may update a tags file, clear cached tags. */
	tag_freematch();

	if (cmd == NULL || *p_sxq == NUL)
	    retval = mch_call_shell(cmd, opt);
	else
	{
	    ncmd = alloc((unsigned)(STRLEN(cmd) + STRLEN(p_sxq) * 2 + 1));
	    if (ncmd != NULL)
	    {
		STRCPY(ncmd, p_sxq);
		STRCAT(ncmd, cmd);
		STRCAT(ncmd, p_sxq);
		retval = mch_call_shell(ncmd, opt);
		vim_free(ncmd);
	    }
	    else
		retval = -1;
	}
#ifdef FEAT_GUI
	--hold_gui_events;
#endif
	/*
	 * Check the window size, in case it changed while executing the
	 * external command.
	 */
	shell_resized_check();
    }

#ifdef FEAT_EVAL
    set_vim_var_nr(VV_SHELL_ERROR, (long)retval);
# ifdef FEAT_PROFILE
    if (do_profiling == PROF_YES)
	prof_child_exit(&wait_time);
# endif
#endif

    return retval;
}

/*
 * VISUAL, SELECTMODE and OP_PENDING State are never set, they are equal to
 * NORMAL State with a condition.  This function returns the real State.
 */
    int
get_real_state()
{
    if (State & NORMAL)
    {
#ifdef FEAT_VISUAL
	if (VIsual_active)
	{
	    if (VIsual_select)
		return SELECTMODE;
	    return VISUAL;
	}
	else
#endif
	    if (finish_op)
		return OP_PENDING;
    }
    return State;
}

#if defined(FEAT_MBYTE) || defined(PROTO)
/*
 * Return TRUE if "p" points to just after a path separator.
 * Takes care of multi-byte characters.
 * "b" must point to the start of the file name
 */
    int
after_pathsep(b, p)
    char_u	*b;
    char_u	*p;
{
    return p > b && vim_ispathsep(p[-1])
			     && (!has_mbyte || (*mb_head_off)(b, p - 1) == 0);
}
#endif

/*
 * Return TRUE if file names "f1" and "f2" are in the same directory.
 * "f1" may be a short name, "f2" must be a full path.
 */
    int
same_directory(f1, f2)
    char_u	*f1;
    char_u	*f2;
{
    char_u	ffname[MAXPATHL];
    char_u	*t1;
    char_u	*t2;

    /* safety check */
    if (f1 == NULL || f2 == NULL)
	return FALSE;

    (void)vim_FullName(f1, ffname, MAXPATHL, FALSE);
    t1 = gettail_sep(ffname);
    t2 = gettail_sep(f2);
    return (t1 - ffname == t2 - f2
	     && pathcmp((char *)ffname, (char *)f2, (int)(t1 - ffname)) == 0);
}

#if defined(FEAT_SESSION) || defined(MSWIN) || defined(FEAT_GUI_MAC) \
	|| ((defined(FEAT_GUI_GTK)) \
			&& ( defined(FEAT_WINDOWS) || defined(FEAT_DND)) ) \
	|| defined(FEAT_SUN_WORKSHOP) || defined(FEAT_NETBEANS_INTG) \
	|| defined(PROTO)
/*
 * Change to a file's directory.
 * Caller must call shorten_fnames()!
 * Return OK or FAIL.
 */
    int
vim_chdirfile(fname)
    char_u	*fname;
{
    char_u	dir[MAXPATHL];

    vim_strncpy(dir, fname, MAXPATHL - 1);
    *gettail_sep(dir) = NUL;
    return mch_chdir((char *)dir) == 0 ? OK : FAIL;
}
#endif

#if defined(STAT_IGNORES_SLASH) || defined(PROTO)
/*
 * Check if "name" ends in a slash and is not a directory.
 * Used for systems where stat() ignores a trailing slash on a file name.
 * The Vim code assumes a trailing slash is only ignored for a directory.
 */
    int
illegal_slash(name)
    char *name;
{
    if (name[0] == NUL)
	return FALSE;	    /* no file name is not illegal */
    if (name[strlen(name) - 1] != '/')
	return FALSE;	    /* no trailing slash */
    if (mch_isdir((char_u *)name))
	return FALSE;	    /* trailing slash for a directory */
    return TRUE;
}
#endif

#if defined(CURSOR_SHAPE) || defined(PROTO)

/*
 * Handling of cursor and mouse pointer shapes in various modes.
 */

cursorentry_T shape_table[SHAPE_IDX_COUNT] =
{
    /* The values will be filled in from the 'guicursor' and 'mouseshape'
     * defaults when Vim starts.
     * Adjust the SHAPE_IDX_ defines when making changes! */
    {0,	0, 0, 700L, 400L, 250L, 0, 0, "n", SHAPE_CURSOR+SHAPE_MOUSE},
    {0,	0, 0, 700L, 400L, 250L, 0, 0, "v", SHAPE_CURSOR+SHAPE_MOUSE},
    {0,	0, 0, 700L, 400L, 250L, 0, 0, "i", SHAPE_CURSOR+SHAPE_MOUSE},
    {0,	0, 0, 700L, 400L, 250L, 0, 0, "r", SHAPE_CURSOR+SHAPE_MOUSE},
    {0,	0, 0, 700L, 400L, 250L, 0, 0, "c", SHAPE_CURSOR+SHAPE_MOUSE},
    {0,	0, 0, 700L, 400L, 250L, 0, 0, "ci", SHAPE_CURSOR+SHAPE_MOUSE},
    {0,	0, 0, 700L, 400L, 250L, 0, 0, "cr", SHAPE_CURSOR+SHAPE_MOUSE},
    {0,	0, 0, 700L, 400L, 250L, 0, 0, "o", SHAPE_CURSOR+SHAPE_MOUSE},
    {0,	0, 0, 700L, 400L, 250L, 0, 0, "ve", SHAPE_CURSOR+SHAPE_MOUSE},
    {0,	0, 0,   0L,   0L,   0L, 0, 0, "e", SHAPE_MOUSE},
    {0,	0, 0,   0L,   0L,   0L, 0, 0, "s", SHAPE_MOUSE},
    {0,	0, 0,   0L,   0L,   0L, 0, 0, "sd", SHAPE_MOUSE},
    {0,	0, 0,   0L,   0L,   0L, 0, 0, "vs", SHAPE_MOUSE},
    {0,	0, 0,   0L,   0L,   0L, 0, 0, "vd", SHAPE_MOUSE},
    {0,	0, 0,   0L,   0L,   0L, 0, 0, "m", SHAPE_MOUSE},
    {0,	0, 0,   0L,   0L,   0L, 0, 0, "ml", SHAPE_MOUSE},
    {0,	0, 0, 100L, 100L, 100L, 0, 0, "sm", SHAPE_CURSOR},
};

#ifdef FEAT_MOUSESHAPE
/*
 * Table with names for mouse shapes.  Keep in sync with all the tables for
 * mch_set_mouse_shape()!.
 */
static char * mshape_names[] =
{
    "arrow",	/* default, must be the first one */
    "blank",	/* hidden */
    "beam",
    "updown",
    "udsizing",
    "leftright",
    "lrsizing",
    "busy",
    "no",
    "crosshair",
    "hand1",
    "hand2",
    "pencil",
    "question",
    "rightup-arrow",
    "up-arrow",
    NULL
};
#endif

/*
 * Parse the 'guicursor' option ("what" is SHAPE_CURSOR) or 'mouseshape'
 * ("what" is SHAPE_MOUSE).
 * Returns error message for an illegal option, NULL otherwise.
 */
    char_u *
parse_shape_opt(what)
    int		what;
{
    char_u	*modep;
    char_u	*colonp;
    char_u	*commap;
    char_u	*slashp;
    char_u	*p, *endp;
    int		idx = 0;		/* init for GCC */
    int		all_idx;
    int		len;
    int		i;
    long	n;
    int		found_ve = FALSE;	/* found "ve" flag */
    int		round;

    /*
     * First round: check for errors; second round: do it for real.
     */
    for (round = 1; round <= 2; ++round)
    {
	/*
	 * Repeat for all comma separated parts.
	 */
#ifdef FEAT_MOUSESHAPE
	if (what == SHAPE_MOUSE)
	    modep = p_mouseshape;
	else
#endif
	    modep = p_guicursor;
	while (*modep != NUL)
	{
	    colonp = vim_strchr(modep, ':');
	    if (colonp == NULL)
		return (char_u *)N_("E545: Missing colon");
	    if (colonp == modep)
		return (char_u *)N_("E546: Illegal mode");
	    commap = vim_strchr(modep, ',');

	    /*
	     * Repeat for all mode's before the colon.
	     * For the 'a' mode, we loop to handle all the modes.
	     */
	    all_idx = -1;
	    while (modep < colonp || all_idx >= 0)
	    {
		if (all_idx < 0)
		{
		    /* Find the mode. */
		    if (modep[1] == '-' || modep[1] == ':')
			len = 1;
		    else
			len = 2;
		    if (len == 1 && TOLOWER_ASC(modep[0]) == 'a')
			all_idx = SHAPE_IDX_COUNT - 1;
		    else
		    {
			for (idx = 0; idx < SHAPE_IDX_COUNT; ++idx)
			    if (STRNICMP(modep, shape_table[idx].name, len)
									 == 0)
				break;
			if (idx == SHAPE_IDX_COUNT
				   || (shape_table[idx].used_for & what) == 0)
			    return (char_u *)N_("E546: Illegal mode");
			if (len == 2 && modep[0] == 'v' && modep[1] == 'e')
			    found_ve = TRUE;
		    }
		    modep += len + 1;
		}

		if (all_idx >= 0)
		    idx = all_idx--;
		else if (round == 2)
		{
#ifdef FEAT_MOUSESHAPE
		    if (what == SHAPE_MOUSE)
		    {
			/* Set the default, for the missing parts */
			shape_table[idx].mshape = 0;
		    }
		    else
#endif
		    {
			/* Set the defaults, for the missing parts */
			shape_table[idx].shape = SHAPE_BLOCK;
			shape_table[idx].blinkwait = 700L;
			shape_table[idx].blinkon = 400L;
			shape_table[idx].blinkoff = 250L;
		    }
		}

		/* Parse the part after the colon */
		for (p = colonp + 1; *p && *p != ','; )
		{
#ifdef FEAT_MOUSESHAPE
		    if (what == SHAPE_MOUSE)
		    {
			for (i = 0; ; ++i)
			{
			    if (mshape_names[i] == NULL)
			    {
				if (!VIM_ISDIGIT(*p))
				    return (char_u *)N_("E547: Illegal mouseshape");
				if (round == 2)
				    shape_table[idx].mshape =
					      getdigits(&p) + MSHAPE_NUMBERED;
				else
				    (void)getdigits(&p);
				break;
			    }
			    len = (int)STRLEN(mshape_names[i]);
			    if (STRNICMP(p, mshape_names[i], len) == 0)
			    {
				if (round == 2)
				    shape_table[idx].mshape = i;
				p += len;
				break;
			    }
			}
		    }
		    else /* if (what == SHAPE_MOUSE) */
#endif
		    {
			/*
			 * First handle the ones with a number argument.
			 */
			i = *p;
			len = 0;
			if (STRNICMP(p, "ver", 3) == 0)
			    len = 3;
			else if (STRNICMP(p, "hor", 3) == 0)
			    len = 3;
			else if (STRNICMP(p, "blinkwait", 9) == 0)
			    len = 9;
			else if (STRNICMP(p, "blinkon", 7) == 0)
			    len = 7;
			else if (STRNICMP(p, "blinkoff", 8) == 0)
			    len = 8;
			if (len != 0)
			{
			    p += len;
			    if (!VIM_ISDIGIT(*p))
				return (char_u *)N_("E548: digit expected");
			    n = getdigits(&p);
			    if (len == 3)   /* "ver" or "hor" */
			    {
				if (n == 0)
				    return (char_u *)N_("E549: Illegal percentage");
				if (round == 2)
				{
				    if (TOLOWER_ASC(i) == 'v')
					shape_table[idx].shape = SHAPE_VER;
				    else
					shape_table[idx].shape = SHAPE_HOR;
				    shape_table[idx].percentage = n;
				}
			    }
			    else if (round == 2)
			    {
				if (len == 9)
				    shape_table[idx].blinkwait = n;
				else if (len == 7)
				    shape_table[idx].blinkon = n;
				else
				    shape_table[idx].blinkoff = n;
			    }
			}
			else if (STRNICMP(p, "block", 5) == 0)
			{
			    if (round == 2)
				shape_table[idx].shape = SHAPE_BLOCK;
			    p += 5;
			}
			else	/* must be a highlight group name then */
			{
			    endp = vim_strchr(p, '-');
			    if (commap == NULL)		    /* last part */
			    {
				if (endp == NULL)
				    endp = p + STRLEN(p);   /* find end of part */
			    }
			    else if (endp > commap || endp == NULL)
				endp = commap;
			    slashp = vim_strchr(p, '/');
			    if (slashp != NULL && slashp < endp)
			    {
				/* "group/langmap_group" */
				i = syn_check_group(p, (int)(slashp - p));
				p = slashp + 1;
			    }
			    if (round == 2)
			    {
				shape_table[idx].id = syn_check_group(p,
							     (int)(endp - p));
				shape_table[idx].id_lm = shape_table[idx].id;
				if (slashp != NULL && slashp < endp)
				    shape_table[idx].id = i;
			    }
			    p = endp;
			}
		    } /* if (what != SHAPE_MOUSE) */

		    if (*p == '-')
			++p;
		}
	    }
	    modep = p;
	    if (*modep == ',')
		++modep;
	}
    }

    /* If the 's' flag is not given, use the 'v' cursor for 's' */
    if (!found_ve)
    {
#ifdef FEAT_MOUSESHAPE
	if (what == SHAPE_MOUSE)
	{
	    shape_table[SHAPE_IDX_VE].mshape = shape_table[SHAPE_IDX_V].mshape;
	}
	else
#endif
	{
	    shape_table[SHAPE_IDX_VE].shape = shape_table[SHAPE_IDX_V].shape;
	    shape_table[SHAPE_IDX_VE].percentage =
					 shape_table[SHAPE_IDX_V].percentage;
	    shape_table[SHAPE_IDX_VE].blinkwait =
					  shape_table[SHAPE_IDX_V].blinkwait;
	    shape_table[SHAPE_IDX_VE].blinkon =
					    shape_table[SHAPE_IDX_V].blinkon;
	    shape_table[SHAPE_IDX_VE].blinkoff =
					   shape_table[SHAPE_IDX_V].blinkoff;
	    shape_table[SHAPE_IDX_VE].id = shape_table[SHAPE_IDX_V].id;
	    shape_table[SHAPE_IDX_VE].id_lm = shape_table[SHAPE_IDX_V].id_lm;
	}
    }

    return NULL;
}

# if defined(MCH_CURSOR_SHAPE) || defined(FEAT_GUI) \
	|| defined(FEAT_MOUSESHAPE) || defined(PROTO)
/*
 * Return the index into shape_table[] for the current mode.
 * When "mouse" is TRUE, consider indexes valid for the mouse pointer.
 */
    int
get_shape_idx(mouse)
    int	mouse;
{
#ifdef FEAT_MOUSESHAPE
    if (mouse && (State == HITRETURN || State == ASKMORE))
    {
# ifdef FEAT_GUI
	int x, y;
	gui_mch_getmouse(&x, &y);
	if (Y_2_ROW(y) == Rows - 1)
	    return SHAPE_IDX_MOREL;
# endif
	return SHAPE_IDX_MORE;
    }
    if (mouse && drag_status_line)
	return SHAPE_IDX_SDRAG;
# ifdef FEAT_VERTSPLIT
    if (mouse && drag_sep_line)
	return SHAPE_IDX_VDRAG;
# endif
#endif
    if (!mouse && State == SHOWMATCH)
	return SHAPE_IDX_SM;
#ifdef FEAT_VREPLACE
    if (State & VREPLACE_FLAG)
	return SHAPE_IDX_R;
#endif
    if (State & REPLACE_FLAG)
	return SHAPE_IDX_R;
    if (State & INSERT)
	return SHAPE_IDX_I;
    if (State & CMDLINE)
    {
	if (cmdline_at_end())
	    return SHAPE_IDX_C;
	if (cmdline_overstrike())
	    return SHAPE_IDX_CR;
	return SHAPE_IDX_CI;
    }
    if (finish_op)
	return SHAPE_IDX_O;
#ifdef FEAT_VISUAL
    if (VIsual_active)
    {
	if (*p_sel == 'e')
	    return SHAPE_IDX_VE;
	else
	    return SHAPE_IDX_V;
    }
#endif
    return SHAPE_IDX_N;
}
#endif

# if defined(FEAT_MOUSESHAPE) || defined(PROTO)
static int old_mouse_shape = 0;

/*
 * Set the mouse shape:
 * If "shape" is -1, use shape depending on the current mode,
 * depending on the current state.
 * If "shape" is -2, only update the shape when it's CLINE or STATUS (used
 * when the mouse moves off the status or command line).
 */
    void
update_mouseshape(shape_idx)
    int	shape_idx;
{
    int new_mouse_shape;

    /* Only works in GUI mode. */
    if (!gui.in_use || gui.starting)
	return;

    /* Postpone the updating when more is to come.  Speeds up executing of
     * mappings. */
    if (shape_idx == -1 && char_avail())
    {
	postponed_mouseshape = TRUE;
	return;
    }

    /* When ignoring the mouse don't change shape on the statusline. */
    if (*p_mouse == NUL
	    && (shape_idx == SHAPE_IDX_CLINE
		|| shape_idx == SHAPE_IDX_STATUS
		|| shape_idx == SHAPE_IDX_VSEP))
	shape_idx = -2;

    if (shape_idx == -2
	    && old_mouse_shape != shape_table[SHAPE_IDX_CLINE].mshape
	    && old_mouse_shape != shape_table[SHAPE_IDX_STATUS].mshape
	    && old_mouse_shape != shape_table[SHAPE_IDX_VSEP].mshape)
	return;
    if (shape_idx < 0)
	new_mouse_shape = shape_table[get_shape_idx(TRUE)].mshape;
    else
	new_mouse_shape = shape_table[shape_idx].mshape;
    if (new_mouse_shape != old_mouse_shape)
    {
	mch_set_mouse_shape(new_mouse_shape);
	old_mouse_shape = new_mouse_shape;
    }
    postponed_mouseshape = FALSE;
}
# endif

#endif /* CURSOR_SHAPE */


#ifdef FEAT_CRYPT
/*
 * Optional encryption support.
 * Mohsin Ahmed, mosh@sasi.com, 98-09-24
 * Based on zip/crypt sources.
 *
 * NOTE FOR USA: Since 2000 exporting this code from the USA is allowed to
 * most countries.  There are a few exceptions, but that still should not be a
 * problem since this code was originally created in Europe and India.
 *
 * Blowfish addition originally made by Mohsin Ahmed,
 * http://www.cs.albany.edu/~mosh 2010-03-14
 * Based on blowfish by Bruce Schneier (http://www.schneier.com/blowfish.html)
 * and sha256 by Christophe Devine.
 */

/* from zip.h */

typedef unsigned short ush;	/* unsigned 16-bit value */
typedef unsigned long  ulg;	/* unsigned 32-bit value */

static void make_crc_tab __ARGS((void));

static ulg crc_32_tab[256];

/*
 * Fill the CRC table.
 */
    static void
make_crc_tab()
{
    ulg		s,t,v;
    static int	done = FALSE;

    if (done)
	return;
    for (t = 0; t < 256; t++)
    {
	v = t;
	for (s = 0; s < 8; s++)
	    v = (v >> 1) ^ ((v & 1) * (ulg)0xedb88320L);
	crc_32_tab[t] = v;
    }
    done = TRUE;
}

#define CRC32(c, b) (crc_32_tab[((int)(c) ^ (b)) & 0xff] ^ ((c) >> 8))

static ulg keys[3]; /* keys defining the pseudo-random sequence */

/*
 * Return the next byte in the pseudo-random sequence.
 */
#define DECRYPT_BYTE_ZIP(t) { \
    ush temp; \
 \
    temp = (ush)keys[2] | 2; \
    t = (int)(((unsigned)(temp * (temp ^ 1)) >> 8) & 0xff); \
}

/*
 * Update the encryption keys with the next byte of plain text.
 */
#define UPDATE_KEYS_ZIP(c) { \
    keys[0] = CRC32(keys[0], (c)); \
    keys[1] += keys[0] & 0xff; \
    keys[1] = keys[1] * 134775813L + 1; \
    keys[2] = CRC32(keys[2], (int)(keys[1] >> 24)); \
}

static int crypt_busy = 0;
static ulg saved_keys[3];
static int saved_crypt_method;

/*
 * Return int value for crypt method string:
 * 0 for "zip", the old method.  Also for any non-valid value.
 * 1 for "blowfish".
 */
    int
crypt_method_from_string(s)
    char_u  *s;
{
    return *s == 'b' ? 1 : 0;
}

/*
 * Get the crypt method for buffer "buf" as a number.
 */
    int
get_crypt_method(buf)
    buf_T *buf;
{
    return crypt_method_from_string(*buf->b_p_cm == NUL ? p_cm : buf->b_p_cm);
}

/*
 * Set the crypt method for buffer "buf" to "method" using the int value as
 * returned by crypt_method_from_string().
 */
    void
set_crypt_method(buf, method)
    buf_T   *buf;
    int	    method;
{
    free_string_option(buf->b_p_cm);
    buf->b_p_cm = vim_strsave((char_u *)(method == 0 ? "zip" : "blowfish"));
}

/*
 * Prepare for initializing encryption.  If already doing encryption then save
 * the state.
 * Must always be called symmetrically with crypt_pop_state().
 */
    void
crypt_push_state()
{
    if (crypt_busy == 1)
    {
	/* save the state */
	if (use_crypt_method == 0)
	{
	    saved_keys[0] = keys[0];
	    saved_keys[1] = keys[1];
	    saved_keys[2] = keys[2];
	}
	else
	    bf_crypt_save();
	saved_crypt_method = use_crypt_method;
    }
    else if (crypt_busy > 1)
	EMSG2(_(e_intern2), "crypt_push_state()");
    ++crypt_busy;
}

/*
 * End encryption.  If doing encryption before crypt_push_state() then restore
 * the saved state.
 * Must always be called symmetrically with crypt_push_state().
 */
    void
crypt_pop_state()
{
    --crypt_busy;
    if (crypt_busy == 1)
    {
	use_crypt_method = saved_crypt_method;
	if (use_crypt_method == 0)
	{
	    keys[0] = saved_keys[0];
	    keys[1] = saved_keys[1];
	    keys[2] = saved_keys[2];
	}
	else
	    bf_crypt_restore();
    }
}

/*
 * Encrypt "from[len]" into "to[len]".
 * "from" and "to" can be equal to encrypt in place.
 */
    void
crypt_encode(from, len, to)
    char_u	*from;
    size_t	len;
    char_u	*to;
{
    size_t	i;
    int		ztemp, t;

    if (use_crypt_method == 0)
	for (i = 0; i < len; ++i)
	{
	    ztemp = from[i];
	    DECRYPT_BYTE_ZIP(t);
	    UPDATE_KEYS_ZIP(ztemp);
	    to[i] = t ^ ztemp;
	}
    else
	bf_crypt_encode(from, len, to);
}

/*
 * Decrypt "ptr[len]" in place.
 */
    void
crypt_decode(ptr, len)
    char_u	*ptr;
    long	len;
{
    char_u *p;

    if (use_crypt_method == 0)
	for (p = ptr; p < ptr + len; ++p)
	{
	    ush temp;

	    temp = (ush)keys[2] | 2;
	    temp = (int)(((unsigned)(temp * (temp ^ 1)) >> 8) & 0xff);
	    UPDATE_KEYS_ZIP(*p ^= temp);
	}
    else
	bf_crypt_decode(ptr, len);
}

/*
 * Initialize the encryption keys and the random header according to
 * the given password.
 * If "passwd" is NULL or empty, don't do anything.
 */
    void
crypt_init_keys(passwd)
    char_u *passwd;		/* password string with which to modify keys */
{
    if (passwd != NULL && *passwd != NUL)
    {
	if (use_crypt_method == 0)
	{
	    char_u *p;

	    make_crc_tab();
	    keys[0] = 305419896L;
	    keys[1] = 591751049L;
	    keys[2] = 878082192L;
	    for (p = passwd; *p!= NUL; ++p)
	    {
		UPDATE_KEYS_ZIP((int)*p);
	    }
	}
	else
	    bf_crypt_init_keys(passwd);
    }
}

/*
 * Free an allocated crypt key.  Clear the text to make sure it doesn't stay
 * in memory anywhere.
 */
    void
free_crypt_key(key)
    char_u *key;
{
    char_u *p;

    if (key != NULL)
    {
	for (p = key; *p != NUL; ++p)
	    *p = 0;
	vim_free(key);
    }
}

/*
 * Ask the user for a crypt key.
 * When "store" is TRUE, the new key is stored in the 'key' option, and the
 * 'key' option value is returned: Don't free it.
 * When "store" is FALSE, the typed key is returned in allocated memory.
 * Returns NULL on failure.
 */
    char_u *
get_crypt_key(store, twice)
    int		store;
    int		twice;	    /* Ask for the key twice. */
{
    char_u	*p1, *p2 = NULL;
    int		round;

    for (round = 0; ; ++round)
    {
	cmdline_star = TRUE;
	cmdline_row = msg_row;
	p1 = getcmdline_prompt(NUL, round == 0
		? (char_u *)_("Enter encryption key: ")
		: (char_u *)_("Enter same key again: "), 0, EXPAND_NOTHING,
		NULL);
	cmdline_star = FALSE;

	if (p1 == NULL)
	    break;

	if (round == twice)
	{
	    if (p2 != NULL && STRCMP(p1, p2) != 0)
	    {
		MSG(_("Keys don't match!"));
		free_crypt_key(p1);
		free_crypt_key(p2);
		p2 = NULL;
		round = -1;		/* do it again */
		continue;
	    }

	    if (store)
	    {
		set_option_value((char_u *)"key", 0L, p1, OPT_LOCAL);
		free_crypt_key(p1);
		p1 = curbuf->b_p_key;
	    }
	    break;
	}
	p2 = p1;
    }

    /* since the user typed this, no need to wait for return */
    if (msg_didout)
	msg_putchar('\n');
    need_wait_return = FALSE;
    msg_didout = FALSE;

    free_crypt_key(p2);
    return p1;
}

#endif /* FEAT_CRYPT */

/* TODO: make some #ifdef for this */
/*--------[ file searching ]-------------------------------------------------*/
/*
 * File searching functions for 'path', 'tags' and 'cdpath' options.
 * External visible functions:
 * vim_findfile_init()		creates/initialises the search context
 * vim_findfile_free_visited()	free list of visited files/dirs of search
 *				context
 * vim_findfile()		find a file in the search context
 * vim_findfile_cleanup()	cleanup/free search context created by
 *				vim_findfile_init()
 *
 * All static functions and variables start with 'ff_'
 *
 * In general it works like this:
 * First you create yourself a search context by calling vim_findfile_init().
 * It is possible to give a search context from a previous call to
 * vim_findfile_init(), so it can be reused. After this you call vim_findfile()
 * until you are satisfied with the result or it returns NULL. On every call it
 * returns the next file which matches the conditions given to
 * vim_findfile_init(). If it doesn't find a next file it returns NULL.
 *
 * It is possible to call vim_findfile_init() again to reinitialise your search
 * with some new parameters. Don't forget to pass your old search context to
 * it, so it can reuse it and especially reuse the list of already visited
 * directories. If you want to delete the list of already visited directories
 * simply call vim_findfile_free_visited().
 *
 * When you are done call vim_findfile_cleanup() to free the search context.
 *
 * The function vim_findfile_init() has a long comment, which describes the
 * needed parameters.
 *
 *
 *
 * ATTENTION:
 * ==========
 *	Also we use an allocated search context here, this functions are NOT
 *	thread-safe!!!!!
 *
 *	To minimize parameter passing (or because I'm to lazy), only the
 *	external visible functions get a search context as a parameter. This is
 *	then assigned to a static global, which is used throughout the local
 *	functions.
 */

/*
 * type for the directory search stack
 */
typedef struct ff_stack
{
    struct ff_stack	*ffs_prev;

    /* the fix part (no wildcards) and the part containing the wildcards
     * of the search path
     */
    char_u		*ffs_fix_path;
#ifdef FEAT_PATH_EXTRA
    char_u		*ffs_wc_path;
#endif

    /* files/dirs found in the above directory, matched by the first wildcard
     * of wc_part
     */
    char_u		**ffs_filearray;
    int			ffs_filearray_size;
    char_u		ffs_filearray_cur;   /* needed for partly handled dirs */

    /* to store status of partly handled directories
     * 0: we work on this directory for the first time
     * 1: this directory was partly searched in an earlier step
     */
    int			ffs_stage;

    /* How deep are we in the directory tree?
     * Counts backward from value of level parameter to vim_findfile_init
     */
    int			ffs_level;

    /* Did we already expand '**' to an empty string? */
    int			ffs_star_star_empty;
} ff_stack_T;

/*
 * type for already visited directories or files.
 */
typedef struct ff_visited
{
    struct ff_visited	*ffv_next;

#ifdef FEAT_PATH_EXTRA
    /* Visited directories are different if the wildcard string are
     * different. So we have to save it.
     */
    char_u		*ffv_wc_path;
#endif
    /* for unix use inode etc for comparison (needed because of links), else
     * use filename.
     */
#ifdef UNIX
    int			ffv_dev_valid;	/* ffv_dev and ffv_ino were set */
    dev_t		ffv_dev;	/* device number */
    ino_t		ffv_ino;	/* inode number */
#endif
    /* The memory for this struct is allocated according to the length of
     * ffv_fname.
     */
    char_u		ffv_fname[1];	/* actually longer */
} ff_visited_T;

/*
 * We might have to manage several visited lists during a search.
 * This is especially needed for the tags option. If tags is set to:
 *      "./++/tags,./++/TAGS,++/tags"  (replace + with *)
 * So we have to do 3 searches:
 *   1) search from the current files directory downward for the file "tags"
 *   2) search from the current files directory downward for the file "TAGS"
 *   3) search from Vims current directory downwards for the file "tags"
 * As you can see, the first and the third search are for the same file, so for
 * the third search we can use the visited list of the first search. For the
 * second search we must start from a empty visited list.
 * The struct ff_visited_list_hdr is used to manage a linked list of already
 * visited lists.
 */
typedef struct ff_visited_list_hdr
{
    struct ff_visited_list_hdr	*ffvl_next;

    /* the filename the attached visited list is for */
    char_u			*ffvl_filename;

    ff_visited_T		*ffvl_visited_list;

} ff_visited_list_hdr_T;


/*
 * '**' can be expanded to several directory levels.
 * Set the default maximum depth.
 */
#define FF_MAX_STAR_STAR_EXPAND ((char_u)30)

/*
 * The search context:
 *   ffsc_stack_ptr:	the stack for the dirs to search
 *   ffsc_visited_list: the currently active visited list
 *   ffsc_dir_visited_list: the currently active visited list for search dirs
 *   ffsc_visited_lists_list: the list of all visited lists
 *   ffsc_dir_visited_lists_list: the list of all visited lists for search dirs
 *   ffsc_file_to_search:     the file to search for
 *   ffsc_start_dir:	the starting directory, if search path was relative
 *   ffsc_fix_path:	the fix part of the given path (without wildcards)
 *			Needed for upward search.
 *   ffsc_wc_path:	the part of the given path containing wildcards
 *   ffsc_level:	how many levels of dirs to search downwards
 *   ffsc_stopdirs_v:	array of stop directories for upward search
 *   ffsc_find_what:	FINDFILE_BOTH, FINDFILE_DIR or FINDFILE_FILE
 *   ffsc_tagfile:	searching for tags file, don't use 'suffixesadd'
 */
typedef struct ff_search_ctx_T
{
     ff_stack_T			*ffsc_stack_ptr;
     ff_visited_list_hdr_T	*ffsc_visited_list;
     ff_visited_list_hdr_T	*ffsc_dir_visited_list;
     ff_visited_list_hdr_T	*ffsc_visited_lists_list;
     ff_visited_list_hdr_T	*ffsc_dir_visited_lists_list;
     char_u			*ffsc_file_to_search;
     char_u			*ffsc_start_dir;
     char_u			*ffsc_fix_path;
#ifdef FEAT_PATH_EXTRA
     char_u			*ffsc_wc_path;
     int			ffsc_level;
     char_u			**ffsc_stopdirs_v;
#endif
     int			ffsc_find_what;
     int			ffsc_tagfile;
} ff_search_ctx_T;

/* locally needed functions */
#ifdef FEAT_PATH_EXTRA
static int ff_check_visited __ARGS((ff_visited_T **, char_u *, char_u *));
#else
static int ff_check_visited __ARGS((ff_visited_T **, char_u *));
#endif
static void vim_findfile_free_visited_list __ARGS((ff_visited_list_hdr_T **list_headp));
static void ff_free_visited_list __ARGS((ff_visited_T *vl));
static ff_visited_list_hdr_T* ff_get_visited_list __ARGS((char_u *, ff_visited_list_hdr_T **list_headp));
#ifdef FEAT_PATH_EXTRA
static int ff_wc_equal __ARGS((char_u *s1, char_u *s2));
#endif

static void ff_push __ARGS((ff_search_ctx_T *search_ctx, ff_stack_T *stack_ptr));
static ff_stack_T *ff_pop __ARGS((ff_search_ctx_T *search_ctx));
static void ff_clear __ARGS((ff_search_ctx_T *search_ctx));
static void ff_free_stack_element __ARGS((ff_stack_T *stack_ptr));
#ifdef FEAT_PATH_EXTRA
static ff_stack_T *ff_create_stack_element __ARGS((char_u *, char_u *, int, int));
#else
static ff_stack_T *ff_create_stack_element __ARGS((char_u *, int, int));
#endif
#ifdef FEAT_PATH_EXTRA
static int ff_path_in_stoplist __ARGS((char_u *, int, char_u **));
#endif

static char_u e_pathtoolong[] = N_("E854: path too long for completion");

#if 0
/*
 * if someone likes findfirst/findnext, here are the functions
 * NOT TESTED!!
 */

static void *ff_fn_search_context = NULL;

    char_u *
vim_findfirst(path, filename, level)
    char_u	*path;
    char_u	*filename;
    int		level;
{
    ff_fn_search_context =
	vim_findfile_init(path, filename, NULL, level, TRUE, FALSE,
		ff_fn_search_context, rel_fname);
    if (NULL == ff_fn_search_context)
	return NULL;
    else
	return vim_findnext()
}

    char_u *
vim_findnext()
{
    char_u *ret = vim_findfile(ff_fn_search_context);

    if (NULL == ret)
    {
	vim_findfile_cleanup(ff_fn_search_context);
	ff_fn_search_context = NULL;
    }
    return ret;
}
#endif

/*
 * Initialization routine for vim_findfile().
 *
 * Returns the newly allocated search context or NULL if an error occurred.
 *
 * Don't forget to clean up by calling vim_findfile_cleanup() if you are done
 * with the search context.
 *
 * Find the file 'filename' in the directory 'path'.
 * The parameter 'path' may contain wildcards. If so only search 'level'
 * directories deep. The parameter 'level' is the absolute maximum and is
 * not related to restricts given to the '**' wildcard. If 'level' is 100
 * and you use '**200' vim_findfile() will stop after 100 levels.
 *
 * 'filename' cannot contain wildcards!  It is used as-is, no backslashes to
 * escape special characters.
 *
 * If 'stopdirs' is not NULL and nothing is found downward, the search is
 * restarted on the next higher directory level. This is repeated until the
 * start-directory of a search is contained in 'stopdirs'. 'stopdirs' has the
 * format ";*<dirname>*\(;<dirname>\)*;\=$".
 *
 * If the 'path' is relative, the starting dir for the search is either VIM's
 * current dir or if the path starts with "./" the current files dir.
 * If the 'path' is absolute, the starting dir is that part of the path before
 * the first wildcard.
 *
 * Upward search is only done on the starting dir.
 *
 * If 'free_visited' is TRUE the list of already visited files/directories is
 * cleared. Set this to FALSE if you just want to search from another
 * directory, but want to be sure that no directory from a previous search is
 * searched again. This is useful if you search for a file at different places.
 * The list of visited files/dirs can also be cleared with the function
 * vim_findfile_free_visited().
 *
 * Set the parameter 'find_what' to FINDFILE_DIR if you want to search for
 * directories only, FINDFILE_FILE for files only, FINDFILE_BOTH for both.
 *
 * A search context returned by a previous call to vim_findfile_init() can be
 * passed in the parameter "search_ctx_arg".  This context is reused and
 * reinitialized with the new parameters.  The list of already visited
 * directories from this context is only deleted if the parameter
 * "free_visited" is true.  Be aware that the passed "search_ctx_arg" is freed
 * if the reinitialization fails.
 *
 * If you don't have a search context from a previous call "search_ctx_arg"
 * must be NULL.
 *
 * This function silently ignores a few errors, vim_findfile() will have
 * limited functionality then.
 */
    void *
vim_findfile_init(path, filename, stopdirs, level, free_visited, find_what,
					   search_ctx_arg, tagfile, rel_fname)
    char_u	*path;
    char_u	*filename;
    char_u	*stopdirs UNUSED;
    int		level;
    int		free_visited;
    int		find_what;
    void	*search_ctx_arg;
    int		tagfile;	/* expanding names of tags files */
    char_u	*rel_fname;	/* file name to use for "." */
{
#ifdef FEAT_PATH_EXTRA
    char_u		*wc_part;
#endif
    ff_stack_T		*sptr;
    ff_search_ctx_T	*search_ctx;

    /* If a search context is given by the caller, reuse it, else allocate a
     * new one.
     */
    if (search_ctx_arg != NULL)
	search_ctx = search_ctx_arg;
    else
    {
	search_ctx = (ff_search_ctx_T*)alloc((unsigned)sizeof(ff_search_ctx_T));
	if (search_ctx == NULL)
	    goto error_return;
	vim_memset(search_ctx, 0, sizeof(ff_search_ctx_T));
    }
    search_ctx->ffsc_find_what = find_what;
    search_ctx->ffsc_tagfile = tagfile;

    /* clear the search context, but NOT the visited lists */
    ff_clear(search_ctx);

    /* clear visited list if wanted */
    if (free_visited == TRUE)
	vim_findfile_free_visited(search_ctx);
    else
    {
	/* Reuse old visited lists. Get the visited list for the given
	 * filename. If no list for the current filename exists, creates a new
	 * one. */
	search_ctx->ffsc_visited_list = ff_get_visited_list(filename,
					&search_ctx->ffsc_visited_lists_list);
	if (search_ctx->ffsc_visited_list == NULL)
	    goto error_return;
	search_ctx->ffsc_dir_visited_list = ff_get_visited_list(filename,
				    &search_ctx->ffsc_dir_visited_lists_list);
	if (search_ctx->ffsc_dir_visited_list == NULL)
	    goto error_return;
    }

    if (ff_expand_buffer == NULL)
    {
	ff_expand_buffer = (char_u*)alloc(MAXPATHL);
	if (ff_expand_buffer == NULL)
	    goto error_return;
    }

    /* Store information on starting dir now if path is relative.
     * If path is absolute, we do that later.  */
    if (path[0] == '.'
	    && (vim_ispathsep(path[1]) || path[1] == NUL)
	    && (!tagfile || vim_strchr(p_cpo, CPO_DOTTAG) == NULL)
	    && rel_fname != NULL)
    {
	int	len = (int)(gettail(rel_fname) - rel_fname);

	if (!vim_isAbsName(rel_fname) && len + 1 < MAXPATHL)
	{
	    /* Make the start dir an absolute path name. */
	    vim_strncpy(ff_expand_buffer, rel_fname, len);
	    search_ctx->ffsc_start_dir = FullName_save(ff_expand_buffer, FALSE);
	}
	else
	    search_ctx->ffsc_start_dir = vim_strnsave(rel_fname, len);
	if (search_ctx->ffsc_start_dir == NULL)
	    goto error_return;
	if (*++path != NUL)
	    ++path;
    }
    else if (*path == NUL || !vim_isAbsName(path))
    {
#ifdef BACKSLASH_IN_FILENAME
	/* "c:dir" needs "c:" to be expanded, otherwise use current dir */
	if (*path != NUL && path[1] == ':')
	{
	    char_u  drive[3];

	    drive[0] = path[0];
	    drive[1] = ':';
	    drive[2] = NUL;
	    if (vim_FullName(drive, ff_expand_buffer, MAXPATHL, TRUE) == FAIL)
		goto error_return;
	    path += 2;
	}
	else
#endif
	if (mch_dirname(ff_expand_buffer, MAXPATHL) == FAIL)
	    goto error_return;

	search_ctx->ffsc_start_dir = vim_strsave(ff_expand_buffer);
	if (search_ctx->ffsc_start_dir == NULL)
	    goto error_return;

#ifdef BACKSLASH_IN_FILENAME
	/* A path that starts with "/dir" is relative to the drive, not to the
	 * directory (but not for "//machine/dir").  Only use the drive name. */
	if ((*path == '/' || *path == '\\')
		&& path[1] != path[0]
		&& search_ctx->ffsc_start_dir[1] == ':')
	    search_ctx->ffsc_start_dir[2] = NUL;
#endif
    }

#ifdef FEAT_PATH_EXTRA
    /*
     * If stopdirs are given, split them into an array of pointers.
     * If this fails (mem allocation), there is no upward search at all or a
     * stop directory is not recognized -> continue silently.
     * If stopdirs just contains a ";" or is empty,
     * search_ctx->ffsc_stopdirs_v will only contain a  NULL pointer. This
     * is handled as unlimited upward search.  See function
     * ff_path_in_stoplist() for details.
     */
    if (stopdirs != NULL)
    {
	char_u	*walker = stopdirs;
	int	dircount;

	while (*walker == ';')
	    walker++;

	dircount = 1;
	search_ctx->ffsc_stopdirs_v =
				 (char_u **)alloc((unsigned)sizeof(char_u *));

	if (search_ctx->ffsc_stopdirs_v != NULL)
	{
	    do
	    {
		char_u	*helper;
		void	*ptr;

		helper = walker;
		ptr = vim_realloc(search_ctx->ffsc_stopdirs_v,
					   (dircount + 1) * sizeof(char_u *));
		if (ptr)
		    search_ctx->ffsc_stopdirs_v = ptr;
		else
		    /* ignore, keep what we have and continue */
		    break;
		walker = vim_strchr(walker, ';');
		if (walker)
		{
		    search_ctx->ffsc_stopdirs_v[dircount-1] =
				 vim_strnsave(helper, (int)(walker - helper));
		    walker++;
		}
		else
		    /* this might be "", which means ascent till top
		     * of directory tree.
		     */
		    search_ctx->ffsc_stopdirs_v[dircount-1] =
							  vim_strsave(helper);

		dircount++;

	    } while (walker != NULL);
	    search_ctx->ffsc_stopdirs_v[dircount-1] = NULL;
	}
    }
#endif

#ifdef FEAT_PATH_EXTRA
    search_ctx->ffsc_level = level;

    /* split into:
     *  -fix path
     *  -wildcard_stuff (might be NULL)
     */
    wc_part = vim_strchr(path, '*');
    if (wc_part != NULL)
    {
	int	llevel;
	int	len;
	char	*errpt;

	/* save the fix part of the path */
	search_ctx->ffsc_fix_path = vim_strnsave(path, (int)(wc_part - path));

	/*
	 * copy wc_path and add restricts to the '**' wildcard.
	 * The octet after a '**' is used as a (binary) counter.
	 * So '**3' is transposed to '**^C' ('^C' is ASCII value 3)
	 * or '**76' is transposed to '**N'( 'N' is ASCII value 76).
	 * For EBCDIC you get different character values.
	 * If no restrict is given after '**' the default is used.
	 * Due to this technique the path looks awful if you print it as a
	 * string.
	 */
	len = 0;
	while (*wc_part != NUL)
	{
	    if (len + 5 >= MAXPATHL)
	    {
		EMSG(_(e_pathtoolong));
		break;
	    }
	    if (STRNCMP(wc_part, "**", 2) == 0)
	    {
		ff_expand_buffer[len++] = *wc_part++;
		ff_expand_buffer[len++] = *wc_part++;

		llevel = strtol((char *)wc_part, &errpt, 10);
		if ((char_u *)errpt != wc_part && llevel > 0 && llevel < 255)
		    ff_expand_buffer[len++] = llevel;
		else if ((char_u *)errpt != wc_part && llevel == 0)
		    /* restrict is 0 -> remove already added '**' */
		    len -= 2;
		else
		    ff_expand_buffer[len++] = FF_MAX_STAR_STAR_EXPAND;
		wc_part = (char_u *)errpt;
		if (*wc_part != NUL && !vim_ispathsep(*wc_part))
		{
		    EMSG2(_("E343: Invalid path: '**[number]' must be at the end of the path or be followed by '%s'."), PATHSEPSTR);
		    goto error_return;
		}
	    }
	    else
		ff_expand_buffer[len++] = *wc_part++;
	}
	ff_expand_buffer[len] = NUL;
	search_ctx->ffsc_wc_path = vim_strsave(ff_expand_buffer);

	if (search_ctx->ffsc_wc_path == NULL)
	    goto error_return;
    }
    else
#endif
	search_ctx->ffsc_fix_path = vim_strsave(path);

    if (search_ctx->ffsc_start_dir == NULL)
    {
	/* store the fix part as startdir.
	 * This is needed if the parameter path is fully qualified.
	 */
	search_ctx->ffsc_start_dir = vim_strsave(search_ctx->ffsc_fix_path);
	if (search_ctx->ffsc_start_dir == NULL)
	    goto error_return;
	search_ctx->ffsc_fix_path[0] = NUL;
    }

    /* create an absolute path */
    if (STRLEN(search_ctx->ffsc_start_dir)
			  + STRLEN(search_ctx->ffsc_fix_path) + 3 >= MAXPATHL)
    {
	EMSG(_(e_pathtoolong));
	goto error_return;
    }
    STRCPY(ff_expand_buffer, search_ctx->ffsc_start_dir);
    add_pathsep(ff_expand_buffer);
    STRCAT(ff_expand_buffer, search_ctx->ffsc_fix_path);
    add_pathsep(ff_expand_buffer);

    sptr = ff_create_stack_element(ff_expand_buffer,
#ifdef FEAT_PATH_EXTRA
	    search_ctx->ffsc_wc_path,
#endif
	    level, 0);

    if (sptr == NULL)
	goto error_return;

    ff_push(search_ctx, sptr);

    search_ctx->ffsc_file_to_search = vim_strsave(filename);
    if (search_ctx->ffsc_file_to_search == NULL)
	goto error_return;

    return search_ctx;

error_return:
    /*
     * We clear the search context now!
     * Even when the caller gave us a (perhaps valid) context we free it here,
     * as we might have already destroyed it.
     */
    vim_findfile_cleanup(search_ctx);
    return NULL;
}

#if defined(FEAT_PATH_EXTRA) || defined(PROTO)
/*
 * Get the stopdir string.  Check that ';' is not escaped.
 */
    char_u *
vim_findfile_stopdir(buf)
    char_u	*buf;
{
    char_u	*r_ptr = buf;

    while (*r_ptr != NUL && *r_ptr != ';')
    {
	if (r_ptr[0] == '\\' && r_ptr[1] == ';')
	{
	    /* Overwrite the escape char,
	     * use STRLEN(r_ptr) to move the trailing '\0'. */
	    STRMOVE(r_ptr, r_ptr + 1);
	    r_ptr++;
	}
	r_ptr++;
    }
    if (*r_ptr == ';')
    {
	*r_ptr = 0;
	r_ptr++;
    }
    else if (*r_ptr == NUL)
	r_ptr = NULL;
    return r_ptr;
}
#endif

/*
 * Clean up the given search context. Can handle a NULL pointer.
 */
    void
vim_findfile_cleanup(ctx)
    void	*ctx;
{
    if (ctx == NULL)
	return;

    vim_findfile_free_visited(ctx);
    ff_clear(ctx);
    vim_free(ctx);
}

/*
 * Find a file in a search context.
 * The search context was created with vim_findfile_init() above.
 * Return a pointer to an allocated file name or NULL if nothing found.
 * To get all matching files call this function until you get NULL.
 *
 * If the passed search_context is NULL, NULL is returned.
 *
 * The search algorithm is depth first. To change this replace the
 * stack with a list (don't forget to leave partly searched directories on the
 * top of the list).
 */
    char_u *
vim_findfile(search_ctx_arg)
    void	*search_ctx_arg;
{
    char_u	*file_path;
#ifdef FEAT_PATH_EXTRA
    char_u	*rest_of_wildcards;
    char_u	*path_end = NULL;
#endif
    ff_stack_T	*stackp;
#if defined(FEAT_SEARCHPATH) || defined(FEAT_PATH_EXTRA)
    int		len;
#endif
    int		i;
    char_u	*p;
#ifdef FEAT_SEARCHPATH
    char_u	*suf;
#endif
    ff_search_ctx_T *search_ctx;

    if (search_ctx_arg == NULL)
	return NULL;

    search_ctx = (ff_search_ctx_T *)search_ctx_arg;

    /*
     * filepath is used as buffer for various actions and as the storage to
     * return a found filename.
     */
    if ((file_path = alloc((int)MAXPATHL)) == NULL)
	return NULL;

#ifdef FEAT_PATH_EXTRA
    /* store the end of the start dir -- needed for upward search */
    if (search_ctx->ffsc_start_dir != NULL)
	path_end = &search_ctx->ffsc_start_dir[
					  STRLEN(search_ctx->ffsc_start_dir)];
#endif

#ifdef FEAT_PATH_EXTRA
    /* upward search loop */
    for (;;)
    {
#endif
	/* downward search loop */
	for (;;)
	{
	    /* check if user user wants to stop the search*/
	    ui_breakcheck();
	    if (got_int)
		break;

	    /* get directory to work on from stack */
	    stackp = ff_pop(search_ctx);
	    if (stackp == NULL)
		break;

	    /*
	     * TODO: decide if we leave this test in
	     *
	     * GOOD: don't search a directory(-tree) twice.
	     * BAD:  - check linked list for every new directory entered.
	     *       - check for double files also done below
	     *
	     * Here we check if we already searched this directory.
	     * We already searched a directory if:
	     * 1) The directory is the same.
	     * 2) We would use the same wildcard string.
	     *
	     * Good if you have links on same directory via several ways
	     *  or you have selfreferences in directories (e.g. SuSE Linux 6.3:
	     *  /etc/rc.d/init.d is linked to /etc/rc.d -> endless loop)
	     *
	     * This check is only needed for directories we work on for the
	     * first time (hence stackp->ff_filearray == NULL)
	     */
	    if (stackp->ffs_filearray == NULL
		    && ff_check_visited(&search_ctx->ffsc_dir_visited_list
							  ->ffvl_visited_list,
			stackp->ffs_fix_path
#ifdef FEAT_PATH_EXTRA
			, stackp->ffs_wc_path
#endif
			) == FAIL)
	    {
#ifdef FF_VERBOSE
		if (p_verbose >= 5)
		{
		    verbose_enter_scroll();
		    smsg((char_u *)"Already Searched: %s (%s)",
				   stackp->ffs_fix_path, stackp->ffs_wc_path);
		    /* don't overwrite this either */
		    msg_puts((char_u *)"\n");
		    verbose_leave_scroll();
		}
#endif
		ff_free_stack_element(stackp);
		continue;
	    }
#ifdef FF_VERBOSE
	    else if (p_verbose >= 5)
	    {
		verbose_enter_scroll();
		smsg((char_u *)"Searching: %s (%s)",
				   stackp->ffs_fix_path, stackp->ffs_wc_path);
		/* don't overwrite this either */
		msg_puts((char_u *)"\n");
		verbose_leave_scroll();
	    }
#endif

	    /* check depth */
	    if (stackp->ffs_level <= 0)
	    {
		ff_free_stack_element(stackp);
		continue;
	    }

	    file_path[0] = NUL;

	    /*
	     * If no filearray till now expand wildcards
	     * The function expand_wildcards() can handle an array of paths
	     * and all possible expands are returned in one array. We use this
	     * to handle the expansion of '**' into an empty string.
	     */
	    if (stackp->ffs_filearray == NULL)
	    {
		char_u *dirptrs[2];

		/* we use filepath to build the path expand_wildcards() should
		 * expand.
		 */
		dirptrs[0] = file_path;
		dirptrs[1] = NULL;

		/* if we have a start dir copy it in */
		if (!vim_isAbsName(stackp->ffs_fix_path)
						&& search_ctx->ffsc_start_dir)
		{
		    STRCPY(file_path, search_ctx->ffsc_start_dir);
		    add_pathsep(file_path);
		}

		/* append the fix part of the search path */
		STRCAT(file_path, stackp->ffs_fix_path);
		add_pathsep(file_path);

#ifdef FEAT_PATH_EXTRA
		rest_of_wildcards = stackp->ffs_wc_path;
		if (*rest_of_wildcards != NUL)
		{
		    len = (int)STRLEN(file_path);
		    if (STRNCMP(rest_of_wildcards, "**", 2) == 0)
		    {
			/* pointer to the restrict byte
			 * The restrict byte is not a character!
			 */
			p = rest_of_wildcards + 2;

			if (*p > 0)
			{
			    (*p)--;
			    file_path[len++] = '*';
			}

			if (*p == 0)
			{
			    /* remove '**<numb> from wildcards */
			    STRMOVE(rest_of_wildcards, rest_of_wildcards + 3);
			}
			else
			    rest_of_wildcards += 3;

			if (stackp->ffs_star_star_empty == 0)
			{
			    /* if not done before, expand '**' to empty */
			    stackp->ffs_star_star_empty = 1;
			    dirptrs[1] = stackp->ffs_fix_path;
			}
		    }

		    /*
		     * Here we copy until the next path separator or the end of
		     * the path. If we stop at a path separator, there is
		     * still something else left. This is handled below by
		     * pushing every directory returned from expand_wildcards()
		     * on the stack again for further search.
		     */
		    while (*rest_of_wildcards
			    && !vim_ispathsep(*rest_of_wildcards))
			file_path[len++] = *rest_of_wildcards++;

		    file_path[len] = NUL;
		    if (vim_ispathsep(*rest_of_wildcards))
			rest_of_wildcards++;
		}
#endif

		/*
		 * Expand wildcards like "*" and "$VAR".
		 * If the path is a URL don't try this.
		 */
		if (path_with_url(dirptrs[0]))
		{
		    stackp->ffs_filearray = (char_u **)
					      alloc((unsigned)sizeof(char *));
		    if (stackp->ffs_filearray != NULL
			    && (stackp->ffs_filearray[0]
				= vim_strsave(dirptrs[0])) != NULL)
			stackp->ffs_filearray_size = 1;
		    else
			stackp->ffs_filearray_size = 0;
		}
		else
		    /* Add EW_NOTWILD because the expanded path may contain
		     * wildcard characters that are to be taken literally.
		     * This is a bit of a hack. */
		    expand_wildcards((dirptrs[1] == NULL) ? 1 : 2, dirptrs,
			    &stackp->ffs_filearray_size,
			    &stackp->ffs_filearray,
			    EW_DIR|EW_ADDSLASH|EW_SILENT|EW_NOTWILD);

		stackp->ffs_filearray_cur = 0;
		stackp->ffs_stage = 0;
	    }
#ifdef FEAT_PATH_EXTRA
	    else
		rest_of_wildcards = &stackp->ffs_wc_path[
						 STRLEN(stackp->ffs_wc_path)];
#endif

	    if (stackp->ffs_stage == 0)
	    {
		/* this is the first time we work on this directory */
#ifdef FEAT_PATH_EXTRA
		if (*rest_of_wildcards == NUL)
#endif
		{
		    /*
		     * we don't have further wildcards to expand, so we have to
		     * check for the final file now
		     */
		    for (i = stackp->ffs_filearray_cur;
					  i < stackp->ffs_filearray_size; ++i)
		    {
			if (!path_with_url(stackp->ffs_filearray[i])
				      && !mch_isdir(stackp->ffs_filearray[i]))
			    continue;   /* not a directory */

			/* prepare the filename to be checked for existence
			 * below */
			STRCPY(file_path, stackp->ffs_filearray[i]);
			add_pathsep(file_path);
			STRCAT(file_path, search_ctx->ffsc_file_to_search);

			/*
			 * Try without extra suffix and then with suffixes
			 * from 'suffixesadd'.
			 */
#ifdef FEAT_SEARCHPATH
			len = (int)STRLEN(file_path);
			if (search_ctx->ffsc_tagfile)
			    suf = (char_u *)"";
			else
			    suf = curbuf->b_p_sua;
			for (;;)
#endif
			{
			    /* if file exists and we didn't already find it */
			    if ((path_with_url(file_path)
				  || (mch_getperm(file_path) >= 0
				      && (search_ctx->ffsc_find_what
							      == FINDFILE_BOTH
					  || ((search_ctx->ffsc_find_what
							      == FINDFILE_DIR)
						   == mch_isdir(file_path)))))
#ifndef FF_VERBOSE
				    && (ff_check_visited(
					    &search_ctx->ffsc_visited_list->ffvl_visited_list,
					    file_path
#ifdef FEAT_PATH_EXTRA
					    , (char_u *)""
#endif
					    ) == OK)
#endif
			       )
			    {
#ifdef FF_VERBOSE
				if (ff_check_visited(
					    &search_ctx->ffsc_visited_list->ffvl_visited_list,
					    file_path
#ifdef FEAT_PATH_EXTRA
					    , (char_u *)""
#endif
						    ) == FAIL)
				{
				    if (p_verbose >= 5)
				    {
					verbose_enter_scroll();
					smsg((char_u *)"Already: %s",
								   file_path);
					/* don't overwrite this either */
					msg_puts((char_u *)"\n");
					verbose_leave_scroll();
				    }
				    continue;
				}
#endif

				/* push dir to examine rest of subdirs later */
				stackp->ffs_filearray_cur = i + 1;
				ff_push(search_ctx, stackp);

				if (!path_with_url(file_path))
				    simplify_filename(file_path);
				if (mch_dirname(ff_expand_buffer, MAXPATHL)
									== OK)
				{
				    p = shorten_fname(file_path,
							    ff_expand_buffer);
				    if (p != NULL)
					STRMOVE(file_path, p);
				}
#ifdef FF_VERBOSE
				if (p_verbose >= 5)
				{
				    verbose_enter_scroll();
				    smsg((char_u *)"HIT: %s", file_path);
				    /* don't overwrite this either */
				    msg_puts((char_u *)"\n");
				    verbose_leave_scroll();
				}
#endif
				return file_path;
			    }

#ifdef FEAT_SEARCHPATH
			    /* Not found or found already, try next suffix. */
			    if (*suf == NUL)
				break;
			    copy_option_part(&suf, file_path + len,
							 MAXPATHL - len, ",");
#endif
			}
		    }
		}
#ifdef FEAT_PATH_EXTRA
		else
		{
		    /*
		     * still wildcards left, push the directories for further
		     * search
		     */
		    for (i = stackp->ffs_filearray_cur;
					  i < stackp->ffs_filearray_size; ++i)
		    {
			if (!mch_isdir(stackp->ffs_filearray[i]))
			    continue;	/* not a directory */

			ff_push(search_ctx,
				ff_create_stack_element(
						     stackp->ffs_filearray[i],
						     rest_of_wildcards,
						     stackp->ffs_level - 1, 0));
		    }
		}
#endif
		stackp->ffs_filearray_cur = 0;
		stackp->ffs_stage = 1;
	    }

#ifdef FEAT_PATH_EXTRA
	    /*
	     * if wildcards contains '**' we have to descent till we reach the
	     * leaves of the directory tree.
	     */
	    if (STRNCMP(stackp->ffs_wc_path, "**", 2) == 0)
	    {
		for (i = stackp->ffs_filearray_cur;
					  i < stackp->ffs_filearray_size; ++i)
		{
		    if (fnamecmp(stackp->ffs_filearray[i],
						   stackp->ffs_fix_path) == 0)
			continue; /* don't repush same directory */
		    if (!mch_isdir(stackp->ffs_filearray[i]))
			continue;   /* not a directory */
		    ff_push(search_ctx,
			    ff_create_stack_element(stackp->ffs_filearray[i],
				stackp->ffs_wc_path, stackp->ffs_level - 1, 1));
		}
	    }
#endif

	    /* we are done with the current directory */
	    ff_free_stack_element(stackp);

	}

#ifdef FEAT_PATH_EXTRA
	/* If we reached this, we didn't find anything downwards.
	 * Let's check if we should do an upward search.
	 */
	if (search_ctx->ffsc_start_dir
		&& search_ctx->ffsc_stopdirs_v != NULL && !got_int)
	{
	    ff_stack_T  *sptr;

	    /* is the last starting directory in the stop list? */
	    if (ff_path_in_stoplist(search_ctx->ffsc_start_dir,
		       (int)(path_end - search_ctx->ffsc_start_dir),
		       search_ctx->ffsc_stopdirs_v) == TRUE)
		break;

	    /* cut of last dir */
	    while (path_end > search_ctx->ffsc_start_dir
						  && vim_ispathsep(*path_end))
		path_end--;
	    while (path_end > search_ctx->ffsc_start_dir
					      && !vim_ispathsep(path_end[-1]))
		path_end--;
	    *path_end = 0;
	    path_end--;

	    if (*search_ctx->ffsc_start_dir == 0)
		break;

	    STRCPY(file_path, search_ctx->ffsc_start_dir);
	    add_pathsep(file_path);
	    STRCAT(file_path, search_ctx->ffsc_fix_path);

	    /* create a new stack entry */
	    sptr = ff_create_stack_element(file_path,
		    search_ctx->ffsc_wc_path, search_ctx->ffsc_level, 0);
	    if (sptr == NULL)
		break;
	    ff_push(search_ctx, sptr);
	}
	else
	    break;
    }
#endif

    vim_free(file_path);
    return NULL;
}

/*
 * Free the list of lists of visited files and directories
 * Can handle it if the passed search_context is NULL;
 */
    void
vim_findfile_free_visited(search_ctx_arg)
    void	*search_ctx_arg;
{
    ff_search_ctx_T *search_ctx;

    if (search_ctx_arg == NULL)
	return;

    search_ctx = (ff_search_ctx_T *)search_ctx_arg;
    vim_findfile_free_visited_list(&search_ctx->ffsc_visited_lists_list);
    vim_findfile_free_visited_list(&search_ctx->ffsc_dir_visited_lists_list);
}

    static void
vim_findfile_free_visited_list(list_headp)
    ff_visited_list_hdr_T	**list_headp;
{
    ff_visited_list_hdr_T *vp;

    while (*list_headp != NULL)
    {
	vp = (*list_headp)->ffvl_next;
	ff_free_visited_list((*list_headp)->ffvl_visited_list);

	vim_free((*list_headp)->ffvl_filename);
	vim_free(*list_headp);
	*list_headp = vp;
    }
    *list_headp = NULL;
}

    static void
ff_free_visited_list(vl)
    ff_visited_T *vl;
{
    ff_visited_T *vp;

    while (vl != NULL)
    {
	vp = vl->ffv_next;
#ifdef FEAT_PATH_EXTRA
	vim_free(vl->ffv_wc_path);
#endif
	vim_free(vl);
	vl = vp;
    }
    vl = NULL;
}

/*
 * Returns the already visited list for the given filename. If none is found it
 * allocates a new one.
 */
    static ff_visited_list_hdr_T*
ff_get_visited_list(filename, list_headp)
    char_u			*filename;
    ff_visited_list_hdr_T	**list_headp;
{
    ff_visited_list_hdr_T  *retptr = NULL;

    /* check if a visited list for the given filename exists */
    if (*list_headp != NULL)
    {
	retptr = *list_headp;
	while (retptr != NULL)
	{
	    if (fnamecmp(filename, retptr->ffvl_filename) == 0)
	    {
#ifdef FF_VERBOSE
		if (p_verbose >= 5)
		{
		    verbose_enter_scroll();
		    smsg((char_u *)"ff_get_visited_list: FOUND list for %s",
								    filename);
		    /* don't overwrite this either */
		    msg_puts((char_u *)"\n");
		    verbose_leave_scroll();
		}
#endif
		return retptr;
	    }
	    retptr = retptr->ffvl_next;
	}
    }

#ifdef FF_VERBOSE
    if (p_verbose >= 5)
    {
	verbose_enter_scroll();
	smsg((char_u *)"ff_get_visited_list: new list for %s", filename);
	/* don't overwrite this either */
	msg_puts((char_u *)"\n");
	verbose_leave_scroll();
    }
#endif

    /*
     * if we reach this we didn't find a list and we have to allocate new list
     */
    retptr = (ff_visited_list_hdr_T*)alloc((unsigned)sizeof(*retptr));
    if (retptr == NULL)
	return NULL;

    retptr->ffvl_visited_list = NULL;
    retptr->ffvl_filename = vim_strsave(filename);
    if (retptr->ffvl_filename == NULL)
    {
	vim_free(retptr);
	return NULL;
    }
    retptr->ffvl_next = *list_headp;
    *list_headp = retptr;

    return retptr;
}

#ifdef FEAT_PATH_EXTRA
/*
 * check if two wildcard paths are equal. Returns TRUE or FALSE.
 * They are equal if:
 *  - both paths are NULL
 *  - they have the same length
 *  - char by char comparison is OK
 *  - the only differences are in the counters behind a '**', so
 *    '**\20' is equal to '**\24'
 */
    static int
ff_wc_equal(s1, s2)
    char_u	*s1;
    char_u	*s2;
{
    int		i;

    if (s1 == s2)
	return TRUE;

    if (s1 == NULL || s2 == NULL)
	return FALSE;

    if (STRLEN(s1) != STRLEN(s2))
	return FAIL;

    for (i = 0; s1[i] != NUL && s2[i] != NUL; i++)
    {
	if (s1[i] != s2[i]
#ifdef CASE_INSENSITIVE_FILENAME
		&& TOUPPER_LOC(s1[i]) != TOUPPER_LOC(s2[i])
#endif
		)
	{
	    if (i >= 2)
		if (s1[i-1] == '*' && s1[i-2] == '*')
		    continue;
		else
		    return FAIL;
	    else
		return FAIL;
	}
    }
    return TRUE;
}
#endif

/*
 * maintains the list of already visited files and dirs
 * returns FAIL if the given file/dir is already in the list
 * returns OK if it is newly added
 *
 * TODO: What to do on memory allocation problems?
 *	 -> return TRUE - Better the file is found several times instead of
 *	    never.
 */
    static int
ff_check_visited(visited_list, fname
#ifdef FEAT_PATH_EXTRA
	, wc_path
#endif
	)
    ff_visited_T	**visited_list;
    char_u		*fname;
#ifdef FEAT_PATH_EXTRA
    char_u		*wc_path;
#endif
{
    ff_visited_T	*vp;
#ifdef UNIX
    struct stat		st;
    int			url = FALSE;
#endif

    /* For an URL we only compare the name, otherwise we compare the
     * device/inode (unix) or the full path name (not Unix). */
    if (path_with_url(fname))
    {
	vim_strncpy(ff_expand_buffer, fname, MAXPATHL - 1);
#ifdef UNIX
	url = TRUE;
#endif
    }
    else
    {
	ff_expand_buffer[0] = NUL;
#ifdef UNIX
	if (mch_stat((char *)fname, &st) < 0)
#else
	if (vim_FullName(fname, ff_expand_buffer, MAXPATHL, TRUE) == FAIL)
#endif
	    return FAIL;
    }

    /* check against list of already visited files */
    for (vp = *visited_list; vp != NULL; vp = vp->ffv_next)
    {
	if (
#ifdef UNIX
		!url ? (vp->ffv_dev_valid && vp->ffv_dev == st.st_dev
						  && vp->ffv_ino == st.st_ino)
		     :
#endif
		fnamecmp(vp->ffv_fname, ff_expand_buffer) == 0
	   )
	{
#ifdef FEAT_PATH_EXTRA
	    /* are the wildcard parts equal */
	    if (ff_wc_equal(vp->ffv_wc_path, wc_path) == TRUE)
#endif
		/* already visited */
		return FAIL;
	}
    }

    /*
     * New file/dir.  Add it to the list of visited files/dirs.
     */
    vp = (ff_visited_T *)alloc((unsigned)(sizeof(ff_visited_T)
						 + STRLEN(ff_expand_buffer)));

    if (vp != NULL)
    {
#ifdef UNIX
	if (!url)
	{
	    vp->ffv_dev_valid = TRUE;
	    vp->ffv_ino = st.st_ino;
	    vp->ffv_dev = st.st_dev;
	    vp->ffv_fname[0] = NUL;
	}
	else
	{
	    vp->ffv_dev_valid = FALSE;
#endif
	    STRCPY(vp->ffv_fname, ff_expand_buffer);
#ifdef UNIX
	}
#endif
#ifdef FEAT_PATH_EXTRA
	if (wc_path != NULL)
	    vp->ffv_wc_path = vim_strsave(wc_path);
	else
	    vp->ffv_wc_path = NULL;
#endif

	vp->ffv_next = *visited_list;
	*visited_list = vp;
    }

    return OK;
}

/*
 * create stack element from given path pieces
 */
    static ff_stack_T *
ff_create_stack_element(fix_part,
#ifdef FEAT_PATH_EXTRA
	wc_part,
#endif
	level, star_star_empty)
    char_u	*fix_part;
#ifdef FEAT_PATH_EXTRA
    char_u	*wc_part;
#endif
    int		level;
    int		star_star_empty;
{
    ff_stack_T	*new;

    new = (ff_stack_T *)alloc((unsigned)sizeof(ff_stack_T));
    if (new == NULL)
	return NULL;

    new->ffs_prev	   = NULL;
    new->ffs_filearray	   = NULL;
    new->ffs_filearray_size = 0;
    new->ffs_filearray_cur  = 0;
    new->ffs_stage	   = 0;
    new->ffs_level	   = level;
    new->ffs_star_star_empty = star_star_empty;;

    /* the following saves NULL pointer checks in vim_findfile */
    if (fix_part == NULL)
	fix_part = (char_u *)"";
    new->ffs_fix_path = vim_strsave(fix_part);

#ifdef FEAT_PATH_EXTRA
    if (wc_part == NULL)
	wc_part  = (char_u *)"";
    new->ffs_wc_path = vim_strsave(wc_part);
#endif

    if (new->ffs_fix_path == NULL
#ifdef FEAT_PATH_EXTRA
	    || new->ffs_wc_path == NULL
#endif
	    )
    {
	ff_free_stack_element(new);
	new = NULL;
    }

    return new;
}

/*
 * Push a dir on the directory stack.
 */
    static void
ff_push(search_ctx, stack_ptr)
    ff_search_ctx_T *search_ctx;
    ff_stack_T	    *stack_ptr;
{
    /* check for NULL pointer, not to return an error to the user, but
     * to prevent a crash */
    if (stack_ptr != NULL)
    {
	stack_ptr->ffs_prev = search_ctx->ffsc_stack_ptr;
	search_ctx->ffsc_stack_ptr = stack_ptr;
    }
}

/*
 * Pop a dir from the directory stack.
 * Returns NULL if stack is empty.
 */
    static ff_stack_T *
ff_pop(search_ctx)
    ff_search_ctx_T *search_ctx;
{
    ff_stack_T  *sptr;

    sptr = search_ctx->ffsc_stack_ptr;
    if (search_ctx->ffsc_stack_ptr != NULL)
	search_ctx->ffsc_stack_ptr = search_ctx->ffsc_stack_ptr->ffs_prev;

    return sptr;
}

/*
 * free the given stack element
 */
    static void
ff_free_stack_element(stack_ptr)
    ff_stack_T  *stack_ptr;
{
    /* vim_free handles possible NULL pointers */
    vim_free(stack_ptr->ffs_fix_path);
#ifdef FEAT_PATH_EXTRA
    vim_free(stack_ptr->ffs_wc_path);
#endif

    if (stack_ptr->ffs_filearray != NULL)
	FreeWild(stack_ptr->ffs_filearray_size, stack_ptr->ffs_filearray);

    vim_free(stack_ptr);
}

/*
 * Clear the search context, but NOT the visited list.
 */
    static void
ff_clear(search_ctx)
    ff_search_ctx_T *search_ctx;
{
    ff_stack_T   *sptr;

    /* clear up stack */
    while ((sptr = ff_pop(search_ctx)) != NULL)
	ff_free_stack_element(sptr);

    vim_free(search_ctx->ffsc_file_to_search);
    vim_free(search_ctx->ffsc_start_dir);
    vim_free(search_ctx->ffsc_fix_path);
#ifdef FEAT_PATH_EXTRA
    vim_free(search_ctx->ffsc_wc_path);
#endif

#ifdef FEAT_PATH_EXTRA
    if (search_ctx->ffsc_stopdirs_v != NULL)
    {
	int  i = 0;

	while (search_ctx->ffsc_stopdirs_v[i] != NULL)
	{
	    vim_free(search_ctx->ffsc_stopdirs_v[i]);
	    i++;
	}
	vim_free(search_ctx->ffsc_stopdirs_v);
    }
    search_ctx->ffsc_stopdirs_v = NULL;
#endif

    /* reset everything */
    search_ctx->ffsc_file_to_search = NULL;
    search_ctx->ffsc_start_dir = NULL;
    search_ctx->ffsc_fix_path = NULL;
#ifdef FEAT_PATH_EXTRA
    search_ctx->ffsc_wc_path = NULL;
    search_ctx->ffsc_level = 0;
#endif
}

#ifdef FEAT_PATH_EXTRA
/*
 * check if the given path is in the stopdirs
 * returns TRUE if yes else FALSE
 */
    static int
ff_path_in_stoplist(path, path_len, stopdirs_v)
    char_u	*path;
    int		path_len;
    char_u	**stopdirs_v;
{
    int		i = 0;

    /* eat up trailing path separators, except the first */
    while (path_len > 1 && vim_ispathsep(path[path_len - 1]))
	path_len--;

    /* if no path consider it as match */
    if (path_len == 0)
	return TRUE;

    for (i = 0; stopdirs_v[i] != NULL; i++)
    {
	if ((int)STRLEN(stopdirs_v[i]) > path_len)
	{
	    /* match for parent directory. So '/home' also matches
	     * '/home/rks'. Check for PATHSEP in stopdirs_v[i], else
	     * '/home/r' would also match '/home/rks'
	     */
	    if (fnamencmp(stopdirs_v[i], path, path_len) == 0
		    && vim_ispathsep(stopdirs_v[i][path_len]))
		return TRUE;
	}
	else
	{
	    if (fnamecmp(stopdirs_v[i], path) == 0)
		return TRUE;
	}
    }
    return FALSE;
}
#endif

#if defined(FEAT_SEARCHPATH) || defined(PROTO)
/*
 * Find the file name "ptr[len]" in the path.  Also finds directory names.
 *
 * On the first call set the parameter 'first' to TRUE to initialize
 * the search.  For repeating calls to FALSE.
 *
 * Repeating calls will return other files called 'ptr[len]' from the path.
 *
 * Only on the first call 'ptr' and 'len' are used.  For repeating calls they
 * don't need valid values.
 *
 * If nothing found on the first call the option FNAME_MESS will issue the
 * message:
 *	    'Can't find file "<file>" in path'
 * On repeating calls:
 *	    'No more file "<file>" found in path'
 *
 * options:
 * FNAME_MESS	    give error message when not found
 *
 * Uses NameBuff[]!
 *
 * Returns an allocated string for the file name.  NULL for error.
 *
 */
    char_u *
find_file_in_path(ptr, len, options, first, rel_fname)
    char_u	*ptr;		/* file name */
    int		len;		/* length of file name */
    int		options;
    int		first;		/* use count'th matching file name */
    char_u	*rel_fname;	/* file name searching relative to */
{
    return find_file_in_path_option(ptr, len, options, first,
	    *curbuf->b_p_path == NUL ? p_path : curbuf->b_p_path,
	    FINDFILE_BOTH, rel_fname, curbuf->b_p_sua);
}

static char_u	*ff_file_to_find = NULL;
static void	*fdip_search_ctx = NULL;

#if defined(EXITFREE)
    static void
free_findfile()
{
    vim_free(ff_file_to_find);
    vim_findfile_cleanup(fdip_search_ctx);
}
#endif

/*
 * Find the directory name "ptr[len]" in the path.
 *
 * options:
 * FNAME_MESS	    give error message when not found
 *
 * Uses NameBuff[]!
 *
 * Returns an allocated string for the file name.  NULL for error.
 */
    char_u *
find_directory_in_path(ptr, len, options, rel_fname)
    char_u	*ptr;		/* file name */
    int		len;		/* length of file name */
    int		options;
    char_u	*rel_fname;	/* file name searching relative to */
{
    return find_file_in_path_option(ptr, len, options, TRUE, p_cdpath,
				       FINDFILE_DIR, rel_fname, (char_u *)"");
}

    char_u *
find_file_in_path_option(ptr, len, options, first, path_option, find_what, rel_fname, suffixes)
    char_u	*ptr;		/* file name */
    int		len;		/* length of file name */
    int		options;
    int		first;		/* use count'th matching file name */
    char_u	*path_option;	/* p_path or p_cdpath */
    int		find_what;	/* FINDFILE_FILE, _DIR or _BOTH */
    char_u	*rel_fname;	/* file name we are looking relative to. */
    char_u	*suffixes;	/* list of suffixes, 'suffixesadd' option */
{
    static char_u	*dir;
    static int		did_findfile_init = FALSE;
    char_u		save_char;
    char_u		*file_name = NULL;
    char_u		*buf = NULL;
    int			rel_to_curdir;
#ifdef AMIGA
    struct Process	*proc = (struct Process *)FindTask(0L);
    APTR		save_winptr = proc->pr_WindowPtr;

    /* Avoid a requester here for a volume that doesn't exist. */
    proc->pr_WindowPtr = (APTR)-1L;
#endif

    if (first == TRUE)
    {
	/* copy file name into NameBuff, expanding environment variables */
	save_char = ptr[len];
	ptr[len] = NUL;
	expand_env(ptr, NameBuff, MAXPATHL);
	ptr[len] = save_char;

	vim_free(ff_file_to_find);
	ff_file_to_find = vim_strsave(NameBuff);
	if (ff_file_to_find == NULL)	/* out of memory */
	{
	    file_name = NULL;
	    goto theend;
	}
    }

    rel_to_curdir = (ff_file_to_find[0] == '.'
		    && (ff_file_to_find[1] == NUL
			|| vim_ispathsep(ff_file_to_find[1])
			|| (ff_file_to_find[1] == '.'
			    && (ff_file_to_find[2] == NUL
				|| vim_ispathsep(ff_file_to_find[2])))));
    if (vim_isAbsName(ff_file_to_find)
	    /* "..", "../path", "." and "./path": don't use the path_option */
	    || rel_to_curdir
#if defined(MSWIN) || defined(MSDOS) || defined(OS2)
	    /* handle "\tmp" as absolute path */
	    || vim_ispathsep(ff_file_to_find[0])
	    /* handle "c:name" as absolute path */
	    || (ff_file_to_find[0] != NUL && ff_file_to_find[1] == ':')
#endif
#ifdef AMIGA
	    /* handle ":tmp" as absolute path */
	    || ff_file_to_find[0] == ':'
#endif
       )
    {
	/*
	 * Absolute path, no need to use "path_option".
	 * If this is not a first call, return NULL.  We already returned a
	 * filename on the first call.
	 */
	if (first == TRUE)
	{
	    int		l;
	    int		run;

	    if (path_with_url(ff_file_to_find))
	    {
		file_name = vim_strsave(ff_file_to_find);
		goto theend;
	    }

	    /* When FNAME_REL flag given first use the directory of the file.
	     * Otherwise or when this fails use the current directory. */
	    for (run = 1; run <= 2; ++run)
	    {
		l = (int)STRLEN(ff_file_to_find);
		if (run == 1
			&& rel_to_curdir
			&& (options & FNAME_REL)
			&& rel_fname != NULL
			&& STRLEN(rel_fname) + l < MAXPATHL)
		{
		    STRCPY(NameBuff, rel_fname);
		    STRCPY(gettail(NameBuff), ff_file_to_find);
		    l = (int)STRLEN(NameBuff);
		}
		else
		{
		    STRCPY(NameBuff, ff_file_to_find);
		    run = 2;
		}

		/* When the file doesn't exist, try adding parts of
		 * 'suffixesadd'. */
		buf = suffixes;
		for (;;)
		{
		    if (
#ifdef DJGPP
			    /* "C:" by itself will fail for mch_getperm(),
			     * assume it's always valid. */
			    (find_what != FINDFILE_FILE && NameBuff[0] != NUL
				  && NameBuff[1] == ':'
				  && NameBuff[2] == NUL) ||
#endif
			    (mch_getperm(NameBuff) >= 0
			     && (find_what == FINDFILE_BOTH
				 || ((find_what == FINDFILE_DIR)
						    == mch_isdir(NameBuff)))))
		    {
			file_name = vim_strsave(NameBuff);
			goto theend;
		    }
		    if (*buf == NUL)
			break;
		    copy_option_part(&buf, NameBuff + l, MAXPATHL - l, ",");
		}
	    }
	}
    }
    else
    {
	/*
	 * Loop over all paths in the 'path' or 'cdpath' option.
	 * When "first" is set, first setup to the start of the option.
	 * Otherwise continue to find the next match.
	 */
	if (first == TRUE)
	{
	    /* vim_findfile_free_visited can handle a possible NULL pointer */
	    vim_findfile_free_visited(fdip_search_ctx);
	    dir = path_option;
	    did_findfile_init = FALSE;
	}

	for (;;)
	{
	    if (did_findfile_init)
	    {
		file_name = vim_findfile(fdip_search_ctx);
		if (file_name != NULL)
		    break;

		did_findfile_init = FALSE;
	    }
	    else
	    {
		char_u  *r_ptr;

		if (dir == NULL || *dir == NUL)
		{
		    /* We searched all paths of the option, now we can
		     * free the search context. */
		    vim_findfile_cleanup(fdip_search_ctx);
		    fdip_search_ctx = NULL;
		    break;
		}

		if ((buf = alloc((int)(MAXPATHL))) == NULL)
		    break;

		/* copy next path */
		buf[0] = 0;
		copy_option_part(&dir, buf, MAXPATHL, " ,");

#ifdef FEAT_PATH_EXTRA
		/* get the stopdir string */
		r_ptr = vim_findfile_stopdir(buf);
#else
		r_ptr = NULL;
#endif
		fdip_search_ctx = vim_findfile_init(buf, ff_file_to_find,
					    r_ptr, 100, FALSE, find_what,
					   fdip_search_ctx, FALSE, rel_fname);
		if (fdip_search_ctx != NULL)
		    did_findfile_init = TRUE;
		vim_free(buf);
	    }
	}
    }
    if (file_name == NULL && (options & FNAME_MESS))
    {
	if (first == TRUE)
	{
	    if (find_what == FINDFILE_DIR)
		EMSG2(_("E344: Can't find directory \"%s\" in cdpath"),
			ff_file_to_find);
	    else
		EMSG2(_("E345: Can't find file \"%s\" in path"),
			ff_file_to_find);
	}
	else
	{
	    if (find_what == FINDFILE_DIR)
		EMSG2(_("E346: No more directory \"%s\" found in cdpath"),
			ff_file_to_find);
	    else
		EMSG2(_("E347: No more file \"%s\" found in path"),
			ff_file_to_find);
	}
    }

theend:
#ifdef AMIGA
    proc->pr_WindowPtr = save_winptr;
#endif
    return file_name;
}

#endif /* FEAT_SEARCHPATH */

/*
 * Change directory to "new_dir".  If FEAT_SEARCHPATH is defined, search
 * 'cdpath' for relative directory names, otherwise just mch_chdir().
 */
    int
vim_chdir(new_dir)
    char_u	*new_dir;
{
#ifndef FEAT_SEARCHPATH
    return mch_chdir((char *)new_dir);
#else
    char_u	*dir_name;
    int		r;

    dir_name = find_directory_in_path(new_dir, (int)STRLEN(new_dir),
						FNAME_MESS, curbuf->b_ffname);
    if (dir_name == NULL)
	return -1;
    r = mch_chdir((char *)dir_name);
    vim_free(dir_name);
    return r;
#endif
}

/*
 * Get user name from machine-specific function.
 * Returns the user name in "buf[len]".
 * Some systems are quite slow in obtaining the user name (Windows NT), thus
 * cache the result.
 * Returns OK or FAIL.
 */
    int
get_user_name(buf, len)
    char_u	*buf;
    int		len;
{
    if (username == NULL)
    {
	if (mch_get_user_name(buf, len) == FAIL)
	    return FAIL;
	username = vim_strsave(buf);
    }
    else
	vim_strncpy(buf, username, len - 1);
    return OK;
}

#ifndef HAVE_QSORT
/*
 * Our own qsort(), for systems that don't have it.
 * It's simple and slow.  From the K&R C book.
 */
    void
qsort(base, elm_count, elm_size, cmp)
    void	*base;
    size_t	elm_count;
    size_t	elm_size;
    int (*cmp) __ARGS((const void *, const void *));
{
    char_u	*buf;
    char_u	*p1;
    char_u	*p2;
    int		i, j;
    int		gap;

    buf = alloc((unsigned)elm_size);
    if (buf == NULL)
	return;

    for (gap = elm_count / 2; gap > 0; gap /= 2)
	for (i = gap; i < elm_count; ++i)
	    for (j = i - gap; j >= 0; j -= gap)
	    {
		/* Compare the elements. */
		p1 = (char_u *)base + j * elm_size;
		p2 = (char_u *)base + (j + gap) * elm_size;
		if ((*cmp)((void *)p1, (void *)p2) <= 0)
		    break;
		/* Exchange the elements. */
		mch_memmove(buf, p1, elm_size);
		mch_memmove(p1, p2, elm_size);
		mch_memmove(p2, buf, elm_size);
	    }

    vim_free(buf);
}
#endif

/*
 * Sort an array of strings.
 */
static int
#ifdef __BORLANDC__
_RTLENTRYF
#endif
sort_compare __ARGS((const void *s1, const void *s2));

    static int
#ifdef __BORLANDC__
_RTLENTRYF
#endif
sort_compare(s1, s2)
    const void	*s1;
    const void	*s2;
{
    return STRCMP(*(char **)s1, *(char **)s2);
}

    void
sort_strings(files, count)
    char_u	**files;
    int		count;
{
    qsort((void *)files, (size_t)count, sizeof(char_u *), sort_compare);
}

#if !defined(NO_EXPANDPATH) || defined(PROTO)
/*
 * Compare path "p[]" to "q[]".
 * If "maxlen" >= 0 compare "p[maxlen]" to "q[maxlen]"
 * Return value like strcmp(p, q), but consider path separators.
 */
    int
pathcmp(p, q, maxlen)
    const char *p, *q;
    int maxlen;
{
    int		i;
    const char	*s = NULL;

    for (i = 0; maxlen < 0 || i < maxlen; ++i)
    {
	/* End of "p": check if "q" also ends or just has a slash. */
	if (p[i] == NUL)
	{
	    if (q[i] == NUL)  /* full match */
		return 0;
	    s = q;
	    break;
	}

	/* End of "q": check if "p" just has a slash. */
	if (q[i] == NUL)
	{
	    s = p;
	    break;
	}

	if (
#ifdef CASE_INSENSITIVE_FILENAME
		TOUPPER_LOC(p[i]) != TOUPPER_LOC(q[i])
#else
		p[i] != q[i]
#endif
#ifdef BACKSLASH_IN_FILENAME
		/* consider '/' and '\\' to be equal */
		&& !((p[i] == '/' && q[i] == '\\')
		    || (p[i] == '\\' && q[i] == '/'))
#endif
		)
	{
	    if (vim_ispathsep(p[i]))
		return -1;
	    if (vim_ispathsep(q[i]))
		return 1;
	    return ((char_u *)p)[i] - ((char_u *)q)[i];	    /* no match */
	}
    }
    if (s == NULL)	/* "i" ran into "maxlen" */
	return 0;

    /* ignore a trailing slash, but not "//" or ":/" */
    if (s[i + 1] == NUL
	    && i > 0
	    && !after_pathsep((char_u *)s, (char_u *)s + i)
#ifdef BACKSLASH_IN_FILENAME
	    && (s[i] == '/' || s[i] == '\\')
#else
	    && s[i] == '/'
#endif
       )
	return 0;   /* match with trailing slash */
    if (s == q)
	return -1;	    /* no match */
    return 1;
}
#endif

/*
 * The putenv() implementation below comes from the "screen" program.
 * Included with permission from Juergen Weigert.
 * See pty.c for the copyright notice.
 */

/*
 *  putenv  --	put value into environment
 *
 *  Usage:  i = putenv (string)
 *    int i;
 *    char  *string;
 *
 *  where string is of the form <name>=<value>.
 *  Putenv returns 0 normally, -1 on error (not enough core for malloc).
 *
 *  Putenv may need to add a new name into the environment, or to
 *  associate a value longer than the current value with a particular
 *  name.  So, to make life simpler, putenv() copies your entire
 *  environment into the heap (i.e. malloc()) from the stack
 *  (i.e. where it resides when your process is initiated) the first
 *  time you call it.
 *
 *  (history removed, not very interesting.  See the "screen" sources.)
 */

#if !defined(HAVE_SETENV) && !defined(HAVE_PUTENV)

#define EXTRASIZE 5		/* increment to add to env. size */

static int  envsize = -1;	/* current size of environment */
#ifndef MACOS_CLASSIC
extern
#endif
       char **environ;		/* the global which is your env. */

static int  findenv __ARGS((char *name)); /* look for a name in the env. */
static int  newenv __ARGS((void));	/* copy env. from stack to heap */
static int  moreenv __ARGS((void));	/* incr. size of env. */

    int
putenv(string)
    const char *string;
{
    int	    i;
    char    *p;

    if (envsize < 0)
    {				/* first time putenv called */
	if (newenv() < 0)	/* copy env. to heap */
	    return -1;
    }

    i = findenv((char *)string); /* look for name in environment */

    if (i < 0)
    {				/* name must be added */
	for (i = 0; environ[i]; i++);
	if (i >= (envsize - 1))
	{			/* need new slot */
	    if (moreenv() < 0)
		return -1;
	}
	p = (char *)alloc((unsigned)(strlen(string) + 1));
	if (p == NULL)		/* not enough core */
	    return -1;
	environ[i + 1] = 0;	/* new end of env. */
    }
    else
    {				/* name already in env. */
	p = vim_realloc(environ[i], strlen(string) + 1);
	if (p == NULL)
	    return -1;
    }
    sprintf(p, "%s", string);	/* copy into env. */
    environ[i] = p;

    return 0;
}

    static int
findenv(name)
    char *name;
{
    char    *namechar, *envchar;
    int	    i, found;

    found = 0;
    for (i = 0; environ[i] && !found; i++)
    {
	envchar = environ[i];
	namechar = name;
	while (*namechar && *namechar != '=' && (*namechar == *envchar))
	{
	    namechar++;
	    envchar++;
	}
	found = ((*namechar == '\0' || *namechar == '=') && *envchar == '=');
    }
    return found ? i - 1 : -1;
}

    static int
newenv()
{
    char    **env, *elem;
    int	    i, esize;

#ifdef MACOS
    /* for Mac a new, empty environment is created */
    i = 0;
#else
    for (i = 0; environ[i]; i++)
	;
#endif
    esize = i + EXTRASIZE + 1;
    env = (char **)alloc((unsigned)(esize * sizeof (elem)));
    if (env == NULL)
	return -1;

#ifndef MACOS
    for (i = 0; environ[i]; i++)
    {
	elem = (char *)alloc((unsigned)(strlen(environ[i]) + 1));
	if (elem == NULL)
	    return -1;
	env[i] = elem;
	strcpy(elem, environ[i]);
    }
#endif

    env[i] = 0;
    environ = env;
    envsize = esize;
    return 0;
}

    static int
moreenv()
{
    int	    esize;
    char    **env;

    esize = envsize + EXTRASIZE;
    env = (char **)vim_realloc((char *)environ, esize * sizeof (*env));
    if (env == 0)
	return -1;
    environ = env;
    envsize = esize;
    return 0;
}

# ifdef USE_VIMPTY_GETENV
    char_u *
vimpty_getenv(string)
    const char_u *string;
{
    int i;
    char_u *p;

    if (envsize < 0)
	return NULL;

    i = findenv((char *)string);

    if (i < 0)
	return NULL;

    p = vim_strchr((char_u *)environ[i], '=');
    return (p + 1);
}
# endif

#endif /* !defined(HAVE_SETENV) && !defined(HAVE_PUTENV) */

#if defined(FEAT_EVAL) || defined(FEAT_SPELL) || defined(PROTO)
/*
 * Return 0 for not writable, 1 for writable file, 2 for a dir which we have
 * rights to write into.
 */
    int
filewritable(fname)
    char_u	*fname;
{
    int		retval = 0;
#if defined(UNIX) || defined(VMS)
    int		perm = 0;
#endif

#if defined(UNIX) || defined(VMS)
    perm = mch_getperm(fname);
#endif
#ifndef MACOS_CLASSIC /* TODO: get either mch_writable or mch_access */
    if (
# ifdef WIN3264
	    mch_writable(fname) &&
# else
# if defined(UNIX) || defined(VMS)
	    (perm & 0222) &&
#  endif
# endif
	    mch_access((char *)fname, W_OK) == 0
       )
#endif
    {
	++retval;
	if (mch_isdir(fname))
	    ++retval;
    }
    return retval;
}
#endif

/*
 * Print an error message with one or two "%s" and one or two string arguments.
 * This is not in message.c to avoid a warning for prototypes.
 */
    int
emsg3(s, a1, a2)
    char_u *s, *a1, *a2;
{
    if (emsg_not_now())
	return TRUE;		/* no error messages at the moment */
#ifdef HAVE_STDARG_H
    vim_snprintf((char *)IObuff, IOSIZE, (char *)s, a1, a2);
#else
    vim_snprintf((char *)IObuff, IOSIZE, (char *)s, (long_u)a1, (long_u)a2);
#endif
    return emsg(IObuff);
}

/*
 * Print an error message with one "%ld" and one long int argument.
 * This is not in message.c to avoid a warning for prototypes.
 */
    int
emsgn(s, n)
    char_u	*s;
    long	n;
{
    if (emsg_not_now())
	return TRUE;		/* no error messages at the moment */
    vim_snprintf((char *)IObuff, IOSIZE, (char *)s, n);
    return emsg(IObuff);
}

#if defined(FEAT_SPELL) || defined(FEAT_PERSISTENT_UNDO) || defined(PROTO)
/*
 * Read 2 bytes from "fd" and turn them into an int, MSB first.
 */
    int
get2c(fd)
    FILE	*fd;
{
    int		n;

    n = getc(fd);
    n = (n << 8) + getc(fd);
    return n;
}

/*
 * Read 3 bytes from "fd" and turn them into an int, MSB first.
 */
    int
get3c(fd)
    FILE	*fd;
{
    int		n;

    n = getc(fd);
    n = (n << 8) + getc(fd);
    n = (n << 8) + getc(fd);
    return n;
}

/*
 * Read 4 bytes from "fd" and turn them into an int, MSB first.
 */
    int
get4c(fd)
    FILE	*fd;
{
    int		n;

    n = getc(fd);
    n = (n << 8) + getc(fd);
    n = (n << 8) + getc(fd);
    n = (n << 8) + getc(fd);
    return n;
}

/*
 * Read 8 bytes from "fd" and turn them into a time_t, MSB first.
 */
    time_t
get8ctime(fd)
    FILE	*fd;
{
    time_t	n = 0;
    int		i;

    for (i = 0; i < 8; ++i)
	n = (n << 8) + getc(fd);
    return n;
}

/*
 * Read a string of length "cnt" from "fd" into allocated memory.
 * Returns NULL when out of memory or unable to read that many bytes.
 */
    char_u *
read_string(fd, cnt)
    FILE	*fd;
    int		cnt;
{
    char_u	*str;
    int		i;
    int		c;

    /* allocate memory */
    str = alloc((unsigned)cnt + 1);
    if (str != NULL)
    {
	/* Read the string.  Quit when running into the EOF. */
	for (i = 0; i < cnt; ++i)
	{
	    c = getc(fd);
	    if (c == EOF)
	    {
		vim_free(str);
		return NULL;
	    }
	    str[i] = c;
	}
	str[i] = NUL;
    }
    return str;
}

/*
 * Write a number to file "fd", MSB first, in "len" bytes.
 */
    int
put_bytes(fd, nr, len)
    FILE    *fd;
    long_u  nr;
    int	    len;
{
    int	    i;

    for (i = len - 1; i >= 0; --i)
	if (putc((int)(nr >> (i * 8)), fd) == EOF)
	    return FAIL;
    return OK;
}

#ifdef _MSC_VER
# if (_MSC_VER <= 1200)
/* This line is required for VC6 without the service pack.  Also see the
 * matching #pragma below. */
 #  pragma optimize("", off)
# endif
#endif

/*
 * Write time_t to file "fd" in 8 bytes.
 */
    void
put_time(fd, the_time)
    FILE	*fd;
    time_t	the_time;
{
    int		c;
    int		i;
    time_t	wtime = the_time;

    /* time_t can be up to 8 bytes in size, more than long_u, thus we
     * can't use put_bytes() here.
     * Another problem is that ">>" may do an arithmetic shift that keeps the
     * sign.  This happens for large values of wtime.  A cast to long_u may
     * truncate if time_t is 8 bytes.  So only use a cast when it is 4 bytes,
     * it's safe to assume that long_u is 4 bytes or more and when using 8
     * bytes the top bit won't be set. */
    for (i = 7; i >= 0; --i)
    {
	if (i + 1 > (int)sizeof(time_t))
	    /* ">>" doesn't work well when shifting more bits than avail */
	    putc(0, fd);
	else
	{
#if defined(SIZEOF_TIME_T) && SIZEOF_TIME_T > 4
	    c = (int)(wtime >> (i * 8));
#else
	    c = (int)((long_u)wtime >> (i * 8));
#endif
	    putc(c, fd);
	}
    }
}

#ifdef _MSC_VER
# if (_MSC_VER <= 1200)
 #  pragma optimize("", on)
# endif
#endif

#endif

#if (defined(FEAT_MBYTE) && defined(FEAT_QUICKFIX)) \
	|| defined(FEAT_SPELL) || defined(PROTO)
/*
 * Return TRUE if string "s" contains a non-ASCII character (128 or higher).
 * When "s" is NULL FALSE is returned.
 */
    int
has_non_ascii(s)
    char_u	*s;
{
    char_u	*p;

    if (s != NULL)
	for (p = s; *p != NUL; ++p)
	    if (*p >= 128)
		return TRUE;
    return FALSE;
}
#endif
