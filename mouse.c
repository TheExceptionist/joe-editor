/* GPM/xterm mouse functions
   Copyright (C) 1999 Jesse McGrew

This file is part of JOE (Joe's Own Editor)

JOE is free software; you can redistribute it and/or modify it under the 
terms of the GNU General Public License as published by the Free Software 
Foundation; either version 1, or (at your option) any later version.  

JOE is distributed in the hope that it will be useful, but WITHOUT ANY 
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more 
details.  

You should have received a copy of the GNU General Public License along with 
JOE; see the file COPYING.  If not, write to the Free Software Foundation, 
675 Mass Ave, Cambridge, MA 02139, USA.  */ 

#include <sys/time.h>

#include "config.h"
#include "b.h"
#include "bw.h"
#include "w.h"
#include "qw.h"
#include "tw.h"
#include "pw.h"
#include "vs.h"
#include "kbd.h"
#include "macro.h"
#include "main.h"
#include "ublock.h"
#include "menu.h"
#include "tty.h"
#include "mouse.h"

int rtbutton=0;			/* use button 3 instead of 1 */
int floatmouse=0;		/* don't fix xcol after tomouse */
int joexterm=0;			/* set if we're using Joe's modified xterm */

static int selecting = 0;	/* Set if we did any selecting */

static int xtmstate;
static int Cb, Cx, Cy;
static int last_msec=0;		/* time in ms when event occurred */
static int clicks;

static void fake_key(int c)
{
	MACRO *m=dokey(maint->curwin->kbd,c);
	int x=maint->curwin->kbd->x;
	maint->curwin->main->kbd->x=x;
	if(x)
		maint->curwin->main->kbd->seq[x-1]=maint->curwin->kbd->seq[x-1];
	if(m)
		exemac(m);
}

/* Translate mouse coordinates */

int mcoord(int x)
{
	if (x>=33 && x<=240)
		return x - 33 + 1;
	else if (x==32)
		return -1 + 1;
	else if (x>240)
		return x - 257 + 1;
}

int uxtmouse(BW *bw)
{
	Cb = ttgetc()-32;
	if (Cb < 0)
		return -1;
	Cx = ttgetc();
	if (Cx < 32)
		return -1;
	Cy = ttgetc();
	if (Cy < 32)
		return -1;

	Cx = mcoord(Cx);
	Cy = mcoord(Cy);

	if ((Cb & 0x41) == 0x40) {
		fake_key(KEY_MWUP);
		return 0;
	}

	if ((Cb & 0x41) == 0x41) {
		fake_key(KEY_MWDOWN);
		return 0;
	}

	if ((Cb & 3) == 3)
		/* button released */
		mouseup(Cx,Cy);
	else if ((Cb & 3) == (rtbutton ? 2 : 0))	/* preferred button */
		if ((Cb & 32) == 0)
			/* button pressed */
			mousedn(Cx,Cy);
		else
			/* drag */
			mousedrag(Cx,Cy);
	else if ((maint->curwin->watom->what & TYPETW ||
	          maint->curwin->watom->what & TYPEPW) &&
	          joexterm && (Cb & 3) == 1)		/* Paste */
		ttputs(US "\33[y");
	return 0;
}

static int mnow()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void mousedn(int x,int y)
{
	Cx = x, Cy = y;
	if (last_msec == 0 || mnow() - last_msec > MOUSE_MULTI_THRESH) {
		/* not a multiple click */
		clicks=1;
		fake_key(KEY_MDOWN);
	} else if(clicks==1) {
		/* double click */
		clicks=2;
		fake_key(KEY_M2DOWN);
	} else if(clicks=2) {
		/* triple click */
		clicks=3;
		fake_key(KEY_M3DOWN);
	} else {
		/* start over */
		clicks=1;
		fake_key(KEY_MDOWN);
	}
}

void select_done()
{
	/* Feed text to xterm */
	if (joexterm && markv(1)) {
		long left = markb->xcol;
		long right = markk->xcol;
		P *q = pdup(markb);
		int c;
		ttputs(US "\33[1y");
		while (q->byte < markk->byte) {
			/* Skip until we're within columns */
			while (q->byte < markk->byte && square && (piscol(q) < left || piscol(q) >= right))
				pgetc(q);

			/* Copy text into buffer */
			while (q->byte < markk->byte && (!square || (piscol(q) >= left && piscol(q) < right))) {
				c = pgetc(q);
				ttputc(c);
			}
			/* Add a new line if we went past right edge of column */
			if (square && q->byte<markk->byte && piscol(q) >= right)
				ttputc(13);
		}
		ttputs(US "\33\33");
		prm(q);
	}
}

void mouseup(int x,int y)
{
	struct timeval tv;
	Cx = x, Cy = y;
	if (selecting) {
		select_done();
		selecting = 0;
	}
	switch(clicks) {
		case 1:
			fake_key(KEY_MUP);
			break;
  
		case 2:
			fake_key(KEY_M2UP);
			break;
  
		case 3:
			fake_key(KEY_M3UP);
			break;
	}
	last_msec = mnow();
}

void mousedrag(int x,int y)
{
	Cx = x, Cy = y;
	switch(clicks) {
		case 1:
			fake_key(KEY_MDRAG);
			break;
  
		case 2:
			fake_key(KEY_M2DRAG);
			break;
  
		case 3:
			fake_key(KEY_M3DRAG);
			break;
	}
}

int drag_size; /* Set if we are resizing a window */

int utomouse(BW *xx)
{
	BW *bw;
	int x = Cx - 1, y = Cy - 1;
	W *w = watpos(maint,x,y);
	if (!w)
		return -1;
	maint->curwin = w;
	bw = w->object;
	drag_size = 0;
	if (w->watom->what == TYPETW) {
		/* window has a status line? */
		if (((TW *)bw->object)->staon)
			/* clicked on it? */
			if (y == w->y) {
				if (y != maint->wind)
					drag_size = y;
				return -1;
			} else
				pline(bw->cursor,y-w->y+bw->top->line-1);
		else
			pline(bw->cursor,y-w->y+bw->top->line);
		pcol(bw->cursor,x-w->x+bw->offset);
		if (floatmouse)
			bw->cursor->xcol = x - w->x + bw->offset;
		else
			bw->cursor->xcol = piscol(bw->cursor);
		return 0;
	} else if (w->watom->what == TYPEPW) {
		PW *pw = (PW *)bw->object;
		/* only one line in prompt windows */
		pcol(bw->cursor,x - w->x + bw->offset - pw->promptlen + pw->promptofst);
		bw->cursor->xcol = piscol(bw->cursor);
	} else if (w->watom->what == TYPEMENU) {
		menujump((MENU *)w->object,x - w->x,y - w->y);
		return 0;
	} else return -1;
}

/* same as utomouse but won't change windows, and always floats. puts the
 * position that utomouse would use into tmspos. */
static int tmspos;

static int tomousestay()
{
	BW *bw;
	int x = Cx - 1,y = Cy - 1;
	W *w = watpos(maint,x,y);
	if(!w || w != maint->curwin)
		return -1;
	bw = w->object;
	if (w->watom->what == TYPETW) {
		/* window has a status line? */
		if (((TW *)bw->object)->staon)
			/* clicked on it? */
			if (y == w->y)
				return -1;
			else
				pline(bw->cursor,y - w->y + bw->top->line - 1);
		else
			pline(bw->cursor,y - w->y + bw->top->line);
		pcol(bw->cursor,x - w->x + bw->offset);
		tmspos = bw->cursor->xcol = x-w->x + bw->offset;
		if (!floatmouse)
			tmspos = piscol(bw->cursor);
		return 0;
	} else if (w->watom->what == TYPEPW) {
		PW *pw = (PW *)bw->object;
		/* only one line in prompt windows */
		pcol(bw->cursor,x - w->x + bw->offset - pw->promptlen + pw->promptofst);
		tmspos = bw->cursor->xcol = piscol(bw->cursor);
	} else return -1;
}

static long anchor;		/* byte where mouse was originally pressed */
static long anchorn;		/* near side of the anchored word */
static int marked;		/* mark was set by defmdrag? */
static int reversed;		/* mouse was dragged above the anchor? */

int udefmdown(BW *xx)
{
	BW *bw;
	if (utomouse(xx))
		return -1;
	if ((maint->curwin->watom->what & (TYPEPW | TYPETW)) == 0)
		return 0;
	bw = (BW *)maint->curwin->object;
	anchor = bw->cursor->byte;
	marked = reversed = 0;
}

int udefmdrag(BW *xx)
{
	BW *bw = (BW *)maint->curwin->object;
	int ax = Cx - 1;
	int ay = Cy - 1;
	if (drag_size) {
		while (ay > bw->parent->y) {
			int y = bw->parent->y;
			wgrowdown(bw->parent);
			if (y == bw->parent->y)
				return -1;
		}
		while (ay < bw->parent->y) {
			int y = bw->parent->y;
			wgrowup(bw->parent);
			if (y == bw->parent->y)
				return -1;
		}
		return 0;
	}
	if (!marked)
		marked++, umarkb(bw);
	if (tomousestay())
		return -1;
	selecting = 1;
	if (reversed)
		umarkb(bw);
	else
		umarkk(bw);
	if ((!reversed && bw->cursor->byte < anchor) || (reversed && bw->cursor->byte > anchor)) {
		P *q = pdup(markb);
		int tmp = markb->xcol;
		pset(markb,markk);
		pset(markk,q);
		markb->xcol = markk->xcol;
		markk->xcol = tmp;
		prm(q);
		reversed = !reversed;
	}
	bw->cursor->xcol = tmspos;
}

int udefmup(BW *bw)
{
}

int udefm2down(BW *xx)
{
	BW *bw;
	if (utomouse(xx))
		return -1;
	if (maint->curwin->watom->what & TYPEMENU) {
		return maint->curwin->watom->rtn((MENU *)maint->curwin->object);
	}
	if ((maint->curwin->watom->what & (TYPEPW | TYPETW)) == 0)
		return 0;
	bw = (BW *)maint->curwin->object;
	/* set anchor to left side, anchorn to right side */
	u_goto_prev(bw); anchor = bw->cursor->byte; umarkb(bw); markb->xcol = piscol(markb);
	u_goto_next(bw); anchorn = bw->cursor->byte; umarkk(bw); markk->xcol = piscol(markk);
	reversed = 0;
	bw->cursor->xcol = piscol(bw->cursor);
	selecting = 1;
}

int udefm2drag(BW *xx)
{
	BW *bw=(BW *)maint->curwin->object;
	if (tomousestay())
		return -1;
	if (!reversed && bw->cursor->byte < anchor) {
		pgoto(markk,anchorn);
		markk->xcol = piscol(markk);
		reversed = 1;
	} else if(reversed && bw->cursor->byte > anchorn) {
		pgoto(markb,anchor);
		markb->xcol = piscol(markb);
		reversed = 0;
	}
	bw->cursor->xcol = piscol(bw->cursor);
	if(reversed) {
		if (!pisbol(bw->cursor))
			u_goto_prev(bw), bw->cursor->xcol = piscol(bw->cursor);
		umarkb(bw);
	} else {
		if (!piseol(bw->cursor))
			u_goto_next(bw), bw->cursor->xcol = piscol(bw->cursor);
		umarkk(bw);
	}
}

int udefm2up(BW *bw)
{
}

int udefm3down(BW *xx)
{
	BW *bw;
	if (utomouse(xx))
		return -1;
	if ((maint->curwin->watom->what & (TYPEPW | TYPETW)) == 0)
		return 0;
	bw = (BW *)maint->curwin->object;
	/* set anchor to beginning of line, anchorn to beginning of next line */
	p_goto_bol(bw->cursor); bw->cursor->xcol = piscol(bw->cursor);
	anchor = bw->cursor->byte; umarkb(bw);
	umarkk(bw); pnextl(markk); anchorn = markk->byte;
	reversed = 0;
	bw->cursor->xcol = piscol(bw->cursor);
	selecting = 1;
}

int udefm3drag(BW *xx)
{
	BW *bw = (BW *)maint->curwin->object;
	if (tomousestay())
		return -1;
	if (!reversed && bw->cursor->byte < anchor) {
		pgoto(markk,anchorn);
		markk->xcol = piscol(markk);
		reversed = 1;
	} else if (reversed && bw->cursor->byte > anchorn) {
		pgoto(markb,anchor);
		markb->xcol = piscol(markb);
		reversed = 0;
	}
	p_goto_bol(bw->cursor);
	bw->cursor->xcol = piscol(bw->cursor);
	if(reversed)
		umarkb(bw), markb->xcol = piscol(markb);
	else
		umarkk(bw), pnextl(markk), markk->xcol = markk->xcol = piscol(markk);
}

int udefm3up(BW *bw)
{
}

void mouseopen()
{
#ifdef MOUSE_XTERM
	if (usexmouse) {
		ttputs(US "\33[?1002h");
		if (joexterm)
			ttputs(US "\33[?2004h");
		ttflsh();
	}
#endif
}

void mouseclose()
{
#ifdef MOUSE_XTERM
	if (usexmouse) {
		if (joexterm)
			ttputs(US "\33[?2004l");
		ttputs(US "\33[?1002l");
		ttflsh();
	}
#endif
}
