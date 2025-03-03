/* copyright (c) 1996,2007 William R. Pearson */

/*  $Id: dropnnw.c 27 2008-06-30 16:27:31Z pearson $ */
/* $Revision: 111 $  */

/* 4-April-2007 - convert to global alignment */

/* 17-Aug-2006 - removed globals *sapp/last - alignment should be thread safe */

/* 12-Oct-2005 - converted to use a_res and aln for alignment coordinates */

/* 4-Nov-2004 - Diagonal Altivec Smith-Waterman included */

/* 14-May-2003 - modified to return alignment start at 0, rather than
   1, for begin:end alignments

   25-Feb-2003 - modified to support Altivec parallel Smith-Waterman

   22-Sep-2003 - removed Altivec support at request of Sencel lawyers
*/

/* this code uses an implementation of the Smith-Waterman algorithm
   designed by Phil Green, U. of Washington, that is 1.5 - 2X faster
   than my Miller and Myers implementation. */

/* the shortcuts used in this program prevent it from calculating scores
   that are less than the gap penalty for the first residue in a gap. As
   a result this code cannot be used with very large gap penalties, or
   with very short sequences, and probably should not be used with prss3.
*/

/* version 3.2 fixes a subtle bug that was encountered while running
   do_walign() interspersed with do_work().  This happens only with -m
   9 and pvcomplib.  The fix was to more explicitly zero-out ss[] at
   the beginning of do_work.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "defs.h"
#include "param.h"

static char *verstr="6.0 April 2007";

#include "dropgsw2.h"

#define DROP_INTERN
#include "drop_func.h"

struct swstr {int H, E;};

extern void init_karlin(const unsigned char *aa0, int n0, struct pstruct *ppst,
			double *aa0_f, double **kp);
extern int do_karlin(const unsigned char *aa1, int n1,
		     int **pam2, const struct pstruct *ppst,
		     double *aa0_f, double *kar_p, double *lambda, double *H);

extern int
NW_ALIGN(int IW, const unsigned char *B,
      int M, int N,
      int **W, int G, int H, int *res, int *nres,
      struct f_struct *f_str);

static int
FGLOBAL_ALIGN(int *pwaa, const unsigned char *aa1,
	      int n0, int n1,
	      int GG,int HH,
	      struct swstr *ss);

static 
void DISPLAY(const unsigned char *A, const unsigned char *B, 
	     int M, int N,
	     int *S, int AP, int BP, char *sq);

extern void aancpy(char *to, char *from, int count, struct pstruct *ppst);

/* initialize for Smith-Waterman optimal score */

void
init_work (unsigned char *aa0, int n0,
	   struct pstruct *ppst,
	   struct f_struct **f_arg)
{
  int maxn0, ip;
  int *pwaa_s, *pwaa_a;
  int e, f, i, j, l;
  int *res;
  struct f_struct *f_str;
  int **pam2p;
  struct swstr *ss;
  int nsq;

  if (ppst->ext_sq_set) {
    nsq = ppst->nsqx; ip = 1;
  }
  else {
    nsq = ppst->nsq; ip = 0;
  }

  /* initialize range of length appropriate */

  if (ppst->n1_low == 0 ) {ppst->n1_low = (int)(0.8 * (float)n0 + 0.5);}

#if defined(GLOBAL_GLOBAL) && !defined(GLOBAL0)
  if (ppst->n1_high == BIGNUM) {
    ppst->n1_high = (int)(1.25 * (float)n0 - 0.5);
  }
#else
  ppst->n1_high = BIGNUM;
#endif

  /* allocate space for function globals */
  f_str = (struct f_struct *)calloc(1,sizeof(struct f_struct));

  if(ppst->zsflag == 6 || ppst->zsflag == 16) {
    f_str->kar_p = NULL;
    init_karlin(aa0, n0, ppst, &f_str->aa0_f[0], &f_str->kar_p);
  }
  
  /* allocate space for the scoring arrays */
  if ((ss = (struct swstr *) calloc (n0+2, sizeof (struct swstr)))
      == NULL) {
    fprintf (stderr, "cannot allocate ss array %3d\n", n0);
    exit (1);
  }
  ss++;

  f_str->ss = ss;

  /* initialize variable (-S) pam matrix */
  if ((f_str->waa_s= (int *)calloc((nsq+1)*(n0+1),sizeof(int))) == NULL) {
    fprintf(stderr,"cannot allocate waa_s array %3d\n",nsq*n0);
    exit(1);
  }

  /* initialize pam2p[1] pointers */
  if ((f_str->pam2p[1]= (int **)calloc((n0+1),sizeof(int *))) == NULL) {
    fprintf(stderr,"cannot allocate pam2p[1] array %3d\n",n0);
    exit(1);
  }

  pam2p = f_str->pam2p[1];
  if ((pam2p[0]=(int *)calloc((nsq+1)*(n0+1),sizeof(int))) == NULL) {
    fprintf(stderr,"cannot allocate pam2p[1][] array %3d\n",nsq*n0);
    exit(1);
  }

  for (i=1; i<n0; i++) {
    pam2p[i]= pam2p[0] + (i*(nsq+1));
  }

  /* initialize universal (alignment) matrix */
  if ((f_str->waa_a= (int *)calloc((nsq+1)*(n0+1),sizeof(int))) == NULL) {
    fprintf(stderr,"cannot allocate waa_a struct %3d\n",nsq*n0);
    exit(1);
  }
   
  /* initialize pam2p[0] pointers */
  if ((f_str->pam2p[0]= (int **)calloc((n0+1),sizeof(int *))) == NULL) {
    fprintf(stderr,"cannot allocate pam2p[1] array %3d\n",n0);
    exit(1);
  }

  pam2p = f_str->pam2p[0];
  if ((pam2p[0]=(int *)calloc((nsq+1)*(n0+1),sizeof(int))) == NULL) {
    fprintf(stderr,"cannot allocate pam2p[1][] array %3d\n",nsq*n0);
    exit(1);
  }

  for (i=1; i<n0; i++) {
    pam2p[i]= pam2p[0] + (i*(nsq+1));
  }

  /* 
     pwaa effectively has a sequence profile --
     pwaa[0..n0-1] has pam score for residue 0 (-BIGNUM)
     pwaa[n0..2n0-1] has pam scores for residue 1 (A)
     pwaa[2n0..3n-1] has pam scores for residue 2 (R), ...

     thus: pwaa = f_str->waa_s + (*aa1p++)*n0; sets up pwaa so that
     *pwaa++ rapidly moves though the scores of the aa1p[] position
     without further indexing

     For a real sequence profile, pwaa[0..n0-1] vs ['A'] could have
     a different score in each position.
  */

  if (ppst->pam_pssm) {
    pwaa_s = f_str->waa_s;
    pwaa_a = f_str->waa_a;
    for (e = 0; e <=nsq; e++)	{	/* for each residue in the alphabet */
      for (f = 0; f < n0; f++) {	/* for each position in aa0 */
	*pwaa_s++ = f_str->pam2p[ip][f][e] = ppst->pam2p[ip][f][e];
	*pwaa_a++ = f_str->pam2p[0][f][e]  = ppst->pam2p[0][f][e];
      }
    }
  }
  else {	/* initialize scanning matrix */
    pwaa_s = f_str->waa_s;
    pwaa_a = f_str->waa_a;
    for (e = 0; e <=nsq; e++)	/* for each residue in the alphabet */
      for (f = 0; f < n0; f++)	{	/* for each position in aa0 */
	*pwaa_s++ = f_str->pam2p[ip][f][e]= ppst->pam2[ip][aa0[f]][e];
	*pwaa_a++ = f_str->pam2p[0][f][e] = ppst->pam2[0][aa0[f]][e];
      }
  }

  /* these structures are used for producing alignments */

  /* minimum allocation for alignment */
  f_str->max_res = max(3*n0/2,MIN_RES);

  *f_arg = f_str;
}

void close_work (const unsigned char *aa0, int n0,
		 struct pstruct *ppst,
		 struct f_struct **f_arg)
{
  struct f_struct *f_str;

  f_str = *f_arg;

  if (f_str != NULL) {
    if (f_str->kar_p !=NULL) free(f_str->kar_p);
    f_str->ss--;
    free(f_str->ss);
    free(f_str->waa_a);
    free(f_str->pam2p[0][0]);
    free(f_str->pam2p[0]);
    free(f_str->waa_s);
    free(f_str->pam2p[1][0]);
    free(f_str->pam2p[1]);

#if defined(SW_ALTIVEC) || defined(SW_SSE2)
    free(f_str->workspace_memory);
    free(f_str->word_score_memory);
    free(f_str->byte_score_memory);
#endif
    free(f_str);
    *f_arg = NULL;
  }
}


/* pstring1 is a message to the manager, currently 512 */
/*void get_param(struct pstruct *pstr,char *pstring1)*/
void
get_param (struct pstruct *ppst, char **pstring1, char *pstring2) {

  char pg_str[120];
  char psi_str[120];

#ifdef NW_SSE2
  char *pg_desc = "Needelman-Wunsch (SSE2, Michael Farrar 2006)";
#else
#if !defined(GLOBAL0) 
#if defined(GLOBAL_GLOBAL) 
  char *pg_desc = "Global/Global Needleman-Wunsch (2007)";
#else
  char *pg_desc = "Global/Local Needleman-Wunsch (2007)";
#endif
#else
  char *pg_desc = "Global0/Local Needleman-Wunsch (2007)";
#endif
#endif

  strncpy(pg_str, pg_desc,  sizeof(pg_str));

  if (ppst->pam_pssm) { strncpy(psi_str,"-PSI",sizeof(psi_str));}
  else { psi_str[0]='\0';}

  sprintf (pstring1[0], "%s (%s)", pg_str, verstr);
  sprintf (pstring1[1], 
#ifdef OLD_FASTA_GAP
	   "%s matrix%s (%d:%d)%s, gap-penalty: %d/%d",
#else
	   "%s matrix%s (%d:%d)%s, open/ext: %d/%d",
#endif
	   ppst->pamfile, psi_str, ppst->pam_h,ppst->pam_l, 
	   (ppst->ext_sq_set)?"xS":"\0", ppst->gdelval, ppst->ggapval);

   if (pstring2 != NULL) {
#ifdef OLD_FASTA_GAP
     sprintf(pstring2,"; pg_name_alg: %s\n; pg_ver_rel: %s\n; pg_matrix: %s (%d:%d)%s\n; pg_gap-pen: %d %d\n",
#else
     sprintf(pstring2,"; pg_name_alg: %s\n; pg_ver_rel: %s\n; pg_matrix: %s (%d:%d)%s\n; pg_open-ext: %d %d\n",
#endif
	     pg_str,verstr,psi_str,ppst->pam_h,ppst->pam_l, 
	     (ppst->ext_sq_set)?"xS":"\0",ppst->gdelval,ppst->ggapval);
   }
}

void do_work (const unsigned char *aa0, int n0,
	      const unsigned char *aa1, int n1,
	      int frame,
	      const struct pstruct *ppst, struct f_struct *f_str,
	      int qr_flg, struct rstruct *rst)
{
  int     score;
  double lambda, H;
  int i;
  
  score = FGLOBAL_ALIGN(f_str->waa_s,aa1,n0,n1,
#ifdef OLD_FASTA_GAP
                       -(ppst->gdelval - ppst->ggapval),
#else
                       -ppst->gdelval,
#endif
                       -ppst->ggapval,f_str->ss);

  rst->score[0] = score;

  if(( ppst->zsflag == 6 || ppst->zsflag == 16) &&
     (do_karlin(aa1, n1, ppst->pam2[0], ppst,f_str->aa0_f, 
		f_str->kar_p, &lambda, &H)>0)) {
    rst->comp = 1.0/lambda;
    rst->H = H;
  }
  else {rst->comp = rst->H = -1.0;}

}

#define gap(k)  ((k) <= 0 ? 0 : g+h*(k))	/* k-symbol indel cost */

static int
FGLOBAL_ALIGN(int *waa, const unsigned char *aa1,
	      int n0, int n1,
	      int q, int r,
	      struct swstr *ss)
{
  const unsigned char *aa1p;
  register int *pwaa;
  int i,j;
  struct swstr *ssj;
  int t;
  int e, f, h, p;
  int qr;
  int score;
  int ij, max_col, max_ij;

  /* q - gap open is positve */
  /* r - gap extend is positive */

  qr = q+r;

  score = -BIGNUM;

  /* initialize 0th row */
  ss[0].H = 0;
  ss[0].E = t = -q;
  for (ssj = ss+1; ssj <= ss+n0 ; ssj++) {
    ssj->H = t = t - r;
    ssj->E = t - q;
  }

  aa1p = aa1;
  t = -q;
  while (*aa1p) {
    p = ss[0].H;
#if defined(GLOBAL_GLOBAL)
    ss[0].H = h = t = t - r;
#else
    ss[0].H = h = t = 0;
#endif
    f = t - q;
    pwaa = waa + (*aa1p++ * n0);
    for (ssj = ss+1; ssj <= ss+n0; ssj++) {	/* go across query */
      if ((h =   h    - qr) > (f =   f    - r)) f = h;
      if ((h = ssj->H - qr) > (e = ssj->E - r)) e = h;
      h = p + *pwaa++;
      if (h < f) h = f;
      if (h < e) h = e;
      p = ssj->H;
      ssj->H = h;
      ssj->E = e;
    }
#if !defined(GLOBAL_GLOBAL)
    if (h > score) {
      score = h;	/* at end of query, update score */
    }
#endif
  }				/* done with forward pass */
#ifdef GLOBAL_GLOBAL
  score = h;
#endif
  return score;
}

void do_opt (const unsigned char *aa0, int n0,
	     const unsigned char *aa1, int n1,
	     int frame,
	     struct pstruct *ppst, struct f_struct *f_str,
	     struct rstruct *rst)
{
}

void
do_walign (const unsigned char *aa0, int n0,
	   const unsigned char *aa1, int n1,
	   int frame,
	   struct pstruct *ppst, 
	   struct f_struct *f_str, 
	   struct a_res_str *a_res,
	   int *have_ares)
{
  const unsigned char *aa0p, *aa1p;
  register int *pwaa;
  register int i, j;
  register struct swstr *ssj;
  struct swstr *ss;
  int *res, *waa, **pam2p;
  int e, f, h, p;
  int q, r, qr, t;
  int     score;
  int cost, I, J, K, L;

  /* now we need alignment storage - get it */
  if ((a_res->res = (int *)calloc((size_t)f_str->max_res,sizeof(int)))==NULL) {
    fprintf(stderr," *** cannot allocate alignment results array %d\n",f_str->max_res);
    exit(1);
  }
  
  *have_ares = 0x3;	/* set 0x2 bit to indicate local copy */
  a_res->next = NULL;

  ss = f_str->ss;	/* row vector */
  waa = f_str->waa_a;	/* this time use universal pam2[0] */
  pam2p = f_str->pam2p[0];	/* pam2p profile */

#ifdef OLD_FASTA_GAP
  q = -(ppst->gdelval - ppst->ggapval);
#else
  q = -ppst->gdelval;
#endif

  r = -ppst->ggapval;
  qr = q + r;

  score = -BIGNUM;
  J = n0-1; L = 0;	/* alignments are global in aa0[n0] */

  /* initialize 0th row */
  ss[0].H = 0;
  ss[0].E = t = -q;	/* must be re-initialized because it was
			   filled in the reverse direction in previous
			   invocations */
  for (ssj=ss+1; ssj <= ss+n0 ; ssj++) {
    ssj->H = t = t - r;
    ssj->E = t - q;
  }

  aa1p = aa1;
  i = 0;
  t = -q;
  while (*aa1p) {
    p = ss[0].H;
#ifndef GLOBAL_GLOBAL
    ss[0].H = h = t = 0;
#else
    ss[0].H = h = t = t - r;
#endif
    f = t - q;
    pwaa = waa + (*aa1p++ * n0);
    for (ssj = ss+1; ssj <= ss+n0; ssj++) {
      if ((h =   h     - qr) > /* gap open from left best */
	  /* gap extend from left gapped */
	  (f =   f     - r)) f = h;	/* if better, use new gap opened */
      if ((h = ssj->H - qr) >	/* gap open from up best */
	  /* gap extend from up gap */
	  (e = ssj->E - r)) e = h;	/* if better, use new gap opened */
      h = p + *pwaa++;		/* diagonal match */
      if (h < f ) h = f;	/* left gap better, reset */
      if (h < e ) h = e;	/* up gap better, reset */
      p = ssj->H;		/* save previous best score */
      ssj->H = h;		/* save (new) up diag-matched */
      ssj->E = e;		/* save upper gap opened */
    }
#ifndef GLOBAL_GLOBAL
    if (h > score) {		/* ? new best score at the end of each row */
      score = h;		/* save best */
      I = i;			/* row */
    }
#endif
    /*
    fprintf(stderr," r %d - score: %d ssj[]: %d\n", i,score,
	    ss[(i <= n0) ? i-1 : n0-1].H);
    */
    i++;
  }				/* done with forward pass */

#ifdef GLOBAL_GLOBAL
  cost = score = h;
  K = 0;
  I = n1 - 1;
#else
  /*   fprintf(stderr, " r: %d - score: %d\n", I, score); */

  /* to get the start point, go backwards */
  
  cost = -BIGNUM;
  K = 0;
  ss[n0].H = 0;
  t = -q;
  for (ssj=ss+n0-1; ssj>=ss; ssj--) {
    ssj->H = t = t - r;
    ssj->E= t - q;
  }

  t = 0;
  for (i=I; i>=0; i--) {
    p = ss[n0].H;
    ss[n0].H = h = t = t-r;
    f = t-q;
    for (ssj=ss+J, j= J; j>=0; ssj--, j--) {
      if ((h =   h     - qr) > /* gap open from left best */
	  /* gap extend from left gapped */
	  (f =   f     - r)) f = h;	/* if better, use new gap opened */
      if ((h = ssj->H - qr) >	/* gap open from up best */
	  /* gap extend from up gap */
	  (e = ssj->E - r)) e = h;	/* if better, use new gap opened */
      h = p + pam2p[j][aa1[i]];		/* diagonal match */
      if (h < f ) h = f;	/* left gap better, reset */
      if (h < e ) h = e;	/* up gap better, reset */
      p = ssj->H;		/* save previous best score */
      ssj->H = h;		/* save (new) up diag-matched */
      ssj->E = e;		/* save upper gap opened */

      /*
      f = max (f,h-q)-r;
      ssj->E = max(ssj->E,ssj->H-q)-r;
      h = max(max(ssj->E,f),p+f_str->pam2p[0][j][aa1[i]]);
      p = ssj->H;
      ssj->H=h;
      */
    }
    if (h > cost) {
      cost = h;
      K = i;
      if (cost >= score) goto found;
    }
  }
  /* at this point, ss[0].E has a very high value for good alignments */
 found:
#endif /* not GLOBAL_GLOBAL */

/*   fprintf(stderr," *** %d=%d: L: %3d-%3d/%3d; K: %3d-%3d/%3d\n",score,cost,L,J+1,n0,K,I+1,n1); */

/* in the f_str version, the *res array is already allocated at 4*n0/3 */

  a_res->max0 = J+1; a_res->min0 = L; a_res->max1 = I+1; a_res->min1 = K;
  
/* this code no longer refers to aa0[], it uses pam2p[0][L] instead */
  NW_ALIGN(L,&aa1[K-1],J-L+1,I-K+1,f_str->pam2p[0],q,r,
	   a_res->res,&a_res->nres,f_str);

/*  DISPLAY(&aa0[L-1],&aa1[K-1],J-L+1,I-K+1,res,L,K,ppst->sq); */

/* return *res and nres */

  a_res->sw_score = score;
}


/*
#define XTERNAL
#include "upam.h"

void
print_seq_prof(unsigned char *A, int M,
	       unsigned char *B, int N,
	       int **w, int iw, int dir) {
  char c_max;
  int i_max, j_max, i,j;

  char *c_dir="LRlr";

  for (i=1; i<=min(60,M); i++) {
    fprintf(stderr,"%c",aa[A[i]]);
  }
  fprintf(stderr, - %d\n,M);

  for (i=0; i<min(60,M); i++) {
    i_max = -1;
    for (j=1; j<21; j++) {
      if (w[iw+i][j]> i_max) {
	i_max = w[iw+i][j]; 
	j_max = j;
      }
    }
    fprintf(stderr,"%c",aa[j_max]);
  }
  fputc(':',stderr);

  for (i=1; i<=min(60,N); i++) {
    fprintf(stderr,"%c",aa[B[i]]);
  }

  fprintf(stderr," -%c: %d,%d\n",c_dir[dir],M,N);
}
*/

void
pre_cons(const unsigned char *aa1, int n1, int frame, struct f_struct *f_str) {

#ifdef TFAST
  f_str->n10 = aatran(aa1,f_str->aa1x,n1,frame);
#endif

}

/* aln_func_vals - set up aln.qlfact, qlrev, llfact, llmult, frame, llrev */
/* call from calcons, calc_id, calc_code */
void 
aln_func_vals(int frame, struct a_struct *aln) {

  aln->llfact = aln->llmult = aln->qlfact = 1;
  aln->qlrev = aln->llrev = 0;
  aln->frame = 0;
}

#ifdef PCOMPLIB
#include "p_mw.h"
void
update_params(struct qmng_str *qm_msg, struct pstruct *ppst)
{
  ppst->n0 = qm_msg->n0;
}
#endif
