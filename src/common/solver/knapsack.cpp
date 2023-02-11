//
// Created by hdevarajan on 5/8/18.
//

#include <iostream>

/* ======================================================================
             mulknap.c,  David Pisinger                     feb 1998
   ====================================================================== */

/* This is the MULKNAP algorithm for solving the multiple knapsack
 * problem. The algorithm is described in
 *
 *   D.Pisinger
 *   "An exact algoirthm for large multiple knapsack problems",
 *   accepted for publication, European Journal of Operational
 *   Research (1998)
 *
 * Copyright (c) D.Pisinger 1998.
 * The code may only be used for academic or non-commercial purposes.
 *
 * Further details on the project can also be found in
 *
 *   D.Pisinger
 *   "Algorithms for knapsack problems"
 *   Report 95/1, DIKU, University of Copenhagen
 *   Universitetsparken 1
 *   DK-2100 Copenhagen
 *
 * The code has been tested on a hp9000/735, and conforms with the
 * ANSI-C standard apart from some of the timing routines (which may
 * be removed).
 *
 * printfs and questions are refered to:
 *   David Pisinger, associate professor
 *   DIKU, University of Copenhagen,
 *   Universitetsparken 1,
 *   DK-2100 Copenhagen.
 *   e-mail: pisinger@diku.dk
 *   fax: +45 35 32 14 01
 */

/* ======================================================================
                                  definitions
   ====================================================================== */

#define MAXN 300          /* only reverse sort if n <= MAXN */
#define MAXCAP 30000      /* only tighten if capacity is up to MAXCAP */
#define MAXSTATES 5000000 /* max number of states in dyn prog for KP */
#define SSSTATES 1000000  /* max number of states in dyn prog for SSP */

#include <string.h>

/* ======================================================================
                                   macros
   ====================================================================== */

#define SYNC 5        /* when to switch to linear scan in bins */
#define SORTSTACK 200 /* size of stack for sorting */
#define MINMED 100    /* find exact median if larger size */
#define XITEMS 50     /* tighten constraints when less items */

#define TRUE 1 /* boolean values */
#define FALSE 0

#define LEFT 1 /* expansion of core in given direction */
#define RIGHT 2

#define PARTIATE 1 /* should sort routine partition or sort */
#define SORTALL 2

#define MORE 0 /* four states used in reducewgtset */
#define LAST 1
#define ALLENUM 2
#define FILLED 3

#define MAXV (8 * sizeof(long)) /* number of bits in a long integer */
#define PMAX 1                  /* profit of worlds most efficient item  */
#define WMAX 0                  /* weight of worlds most efficient item  */
#define PMIN 0                  /* profit of worlds least efficient item */
#define WMIN 1                  /* weight of worlds least efficient item */

#define FDET(a1, a2, b1, b2) ((a1) * (ftype)(b2) - (a2) * (ftype)(b1))
#define DET(a1, a2, b1, b2) ((a1) * (ptype)(b2) - (a2) * (ptype)(b1))
#define SWAP(a, b)                                                             \
  {                                                                            \
    register item t;                                                           \
    t = *(a);                                                                  \
    *(a) = *(b);                                                               \
    *(b) = t;                                                                  \
  }
#define SIZE(a) ((int)(((a)->lset + 1) - (a)->fset))

/* ======================================================================
                                 type declarations
   ====================================================================== */

typedef int boolean;         /* logical variable         */
typedef int64_t ntype;       /* number of states         */
typedef int64_t itype;       /* item profits and weights */
typedef int64_t stype;       /* sum of pofit or weight   */
typedef float ftype;         /* product type (sufficient precision) */
typedef double ptype;        /* product type (sufficient precision) */
typedef signed char mtype;   /* number of knapsacks      */
typedef unsigned long btype; /* binary solution vector   */

typedef int (*funcptr)(const void *, const void *);

/* item record */
typedef struct irec {
  itype p; /* profit */
  itype w; /* weight */
  mtype x; /* optimal solution variable */
  mtype y; /* current solution */
  mtype a; /* consider only after a */
  int *xp; /* pointer to solution variable */
} item;

typedef struct { /* i-stack */
  item *f;
  item *l;
} interval;

/* state in dynamic programming */
typedef struct pv {
  stype psum;
  stype wsum;
  btype vect;
} state;

/* partial weight vector */
typedef struct {
  stype wsum; /* weight sum */
  item *mod;  /* modified vector */
} wstate;

/* set of partial vectors */
typedef struct pset {
  ntype size;  /* set size */
  state *fset; /* first element in set */
  state *lset; /* last element in set */
  state *set1; /* first element in array */
  state *setm; /* last element in array */
} partset;

/* set of partial weight vectors */
typedef struct {
  ntype size;   /* set size */
  wstate *fset; /* first element in set */
  wstate *lset; /* last element in set */
} wgtset;

/* tightening record */
typedef struct {
  stype c;      /* capacity of knapsack */
  stype wsmall; /* current weight sum, small items */
  stype wbig;   /* current weight sum, big items */
} trec;

/* all problem information */
typedef struct {
  int type;    /* type of problem           */
  itype range; /* data range                */
  mtype m;     /* number of knapsacks       */
  ntype n;     /* number of items           */
  boolean sim; /* similar capacities        */
  stype *cap;  /* all capacities */
  item *fitem; /* first item in problem     */
  item *litem; /* last item in problem      */

  /* information corresponding to Multiple Knapsack Problem */
  item *h;      /* index of enumeration      */
  item *maxh;   /* largest depth in tree     */
  stype csum;   /* sum of all initial capacities */
  stype psum;   /* profit sum fixed items    */
  stype wsum;   /* weight sum fixed items    */
  itype brp;    /* profit of break item      */
  itype brw;    /* weight of break item      */
  stype psumb2; /* profit sum break item     */
  stype wsumb2; /* weight sum break item     */
  stype z1;     /* current lower bound       */

  /* information corresponding to Subset-sum Problem */
  mtype k;         /* knapsack to be considered */
  item *fitem2;    /* first item restricted problem */
  item *litem2;    /* last item restricted problem */
  item *s2;        /* s+1 is first item enumme. */
  item *t2;        /* t-1 is last item enummer. */
  item *b2;        /* break item for smallest c */
  stype wsuma2;    /* weight sum of all items   */
  stype wsumbw2;   /* weight sum up to b        */
  stype *wcap;     /* cap info of each knapsack */
  wgtset da;       /* set of partial vectors    */
  wgtset db;       /* set of partial vectors    */
  boolean finish2; /* did we reach optimum      */
  stype psum2;     /* solution profit sum       */
  stype wsum2;     /* solution weight sum       */

  /* information corresponding to 0-1 Knapsack Problem */
  item *ftouch;
  item *ltouch;
  item *s;
  item *t;
  item *b;
  item *fpart;
  item *lpart;
  stype wfpart;
  item *fsort;
  item *lsort;
  stype wfsort;
  stype c;
  stype cstar;
  stype wstar; /* weight sum corresponding to zstar */
  stype z;
  stype lb;    /* lower bound on problem. vector y is not changed */
  stype zstar; /* optimal solution value */
  stype zwsum;
  itype ps, ws, pt, wt;

  btype vno;          /* current vector number   */
  item *vitem[MAXV];  /* current last MAXV items */
  item *ovitem[MAXV]; /* optimal set of items    */
  btype ovect;        /* optimal solution vector */

  stype dantzig;
  stype ub;
  stype psumb;
  stype wsumb;
  boolean firsttime;
  boolean welldef;
  partset d; /* set of partial vectors */
  interval *intv1, *intv2;
  interval *intv1b, *intv2b;

  /* repeatedly used arrays */
  state *kpstates;
  wstate *sstatesa1;
  wstate *sstatesa2;
  wstate *sstatesb1;
  wstate *sstatesb2;
  interval *interv;

  /* debug information */
  long curred; /* number of currently reduced */
} allinfo;

/* **********************************************************************
   **********************************************************************
                             Small procedures
   **********************************************************************
   ********************************************************************** */

/* ======================================================================
                            forward declarations
   ====================================================================== */

static void minknap(allinfo *a, item *fitem, stype c, stype z);

/* ======================================================================
                                  timing routines
   ====================================================================== */

/* These timing routines are written for HP-UNIX, but should be portable
 * with minor changes. The timing function "times" should be portable, but
 * since it returns the number of clock-ticks used by the process, one must
 * convert them to seconds. The function "sysconf" returns the number of
 * clock-ticks per second when given the argument _SC_CLK_TCK, but apparently
 * there is no convention on what _SC_CLK_TCK should be (HP-UNIX is using
 * the value 2, while digital UNIX has value 3). The constant is defined
 * in <unistd.h>, but only if _POSIX_SOURCE is defined.
 */

#define _POSIX_SOURCE         /* to read <unistd.h> on digital UNIX */
#define _INCLUDE_POSIX_SOURCE /* to read <unistd.h> on HP-UX */

#include <sys/times.h> /* timing routines */
#include <unistd.h>    /* define the constant _SC_CLK_TCK */

static void give_time(long *time) { /* return the number of milliseconds used */
  struct tms timeend;
  double t1;
  times(&timeend);
  t1 = (double)(timeend.tms_utime) / sysconf(_SC_CLK_TCK);
  *time = t1 * 1000;
}

/* ======================================================================
                                  palloc
   ====================================================================== */

static void
pfree(void *p) { /* semi-own free routine which makes additional test */
  if (p == NULL)
    printf("freeing null");
  free(p);
}

static void *
palloc(long size,
       long no) { /* semi-own alloc routine which makes additional test */
  long *p;
  size *= no;
  if (size == 0)
    size = 1;
  if (size != (size_t)size)
    printf("Alloc too big %ld", size);
  p = static_cast<long *>(malloc(size));
  if (p == NULL)
    printf("no memory size %ld", size);
  return p;
}

/* ======================================================================
                                  findvect
   ====================================================================== */

static state *findvect(stype ws1, state *f1, state *l1) {
  /* find vector i, so that i->wsum <= ws < (i+1)->wsum */
  register state *f, *l, *m;
  register stype ws;

  /* a set should always have at least one vector */
  f = f1;
  l = l1;
  ws = ws1;
  if (f->wsum > ws)
    return NULL;
  if (l->wsum <= ws)
    return l;

  while (l - f > SYNC) {
    m = f + (l - f) / 2;
    if (m->wsum > ws)
      l = m - 1;
    else
      f = m;
  }
  while (l->wsum > ws)
    l--;
  return l;
}

/* ======================================================================
                                push/pop
   ====================================================================== */

static void push(allinfo *a, int side, item *f, item *l) {
  interval *pos;
  if (l + 1 == f)
    return;
  switch (side) {
  case LEFT:
    pos = a->intv1;
    (a->intv1)++;
    break;
  case RIGHT:
    pos = a->intv2;
    (a->intv2)--;
    break;
  }
  if (a->intv1 == a->intv2)
    printf("interval stack full");
  pos->f = f;
  pos->l = l;
}

static void pop(allinfo *a, int side, item **f, item **l) {
  interval *pos;
  switch (side) {
  case LEFT:
    if (a->intv1 == a->intv1b)
      printf("pop left");
    (a->intv1)--;
    pos = a->intv1;
    break;
  case RIGHT:
    if (a->intv2 == a->intv2b)
      printf("pop right");
    (a->intv2)++;
    pos = a->intv2;
    break;
  }
  *f = pos->f;
  *l = pos->l;
}

/* **********************************************************************
   **********************************************************************
                             Subset-sum Problem
   **********************************************************************
   ********************************************************************** */

/* ======================================================================
                                weightsort
   ====================================================================== */

static void weightsort(allinfo *a, item *f, item *l) {
  register itype mw;
  register item *i, *j, *m;
  int d;

  d = l - f + 1;
  if (d > 1) {
    m = f + d / 2;
    if (f->w < m->w)
      SWAP(f, m);
    if (d > 2) {
      if (m->w < l->w) {
        SWAP(m, l);
        if (f->w < m->w)
          SWAP(f, m);
      }
    }
  }

  if (d > 3) {
    mw = m->w;
    i = f;
    j = l;
    for (;;) {
      do
        i++;
      while (i->w > mw);
      do
        j--;
      while (j->w < mw);
      if (i > j)
        break;
      SWAP(i, j);
    }
    weightsort(a, f, i - 1);
    weightsort(a, i, l);
  }
}

/* ======================================================================
                             compress
   ====================================================================== */

static item *compress(item *f, item *l, mtype y) {
  register item *i, *j, *m;
  register mtype y1;

  /* compress items in table such that ordering is respected */
  y1 = y;
  for (i = l, j = l + 1, m = f - 1; i != m; i--) {
    if (i->y != y1) {
      j--;
      if (i != j)
        SWAP(i, j);
    }
  }
  return j - 1;
}

/* ======================================================================
                                   freeitems
   ====================================================================== */

static item *freeitems(allinfo *a, stype *psumfree, item *f, item *l, mtype y) {
  register item *i, *j, *m;
  register stype ps, ws;
  register mtype y1;

  y1 = y + 1;
  ps = 0;
  ws = 0;
  for (i = f, j = f - 1, m = l + 1; i != m; i++) {
    if (i->y == -y1) {
      i->y = 1;
      ps += i->p;
      ws += i->w;
      continue;
    }
    if ((i->y < 0) || (i->a > y1)) {
      j++;
      SWAP(i, j);
      continue;
    }
    i->y = 0;
  }
  a->psum2 -= ps;
  a->wsum2 -= ws;
  *psumfree = ps; /* lower bound for KP */
  return j + 1;
}

/* ======================================================================
                              defitems
   ====================================================================== */

static item *defitems(allinfo *a, item *f, item *l, mtype y) {
  register item *i, *j, *m;
  register mtype y1;
  register stype ps, ws;

  y1 = y;
  ps = 0;
  ws = 0;
  for (i = f, j = f - 1, m = l + 1; i != m; i++) {
    if (i->y == 1) {
      ps += i->p;
      ws += i->w;
      i->y = y1;
      j++;
      if (i != j)
        SWAP(i, j);
    }
  }
  a->psum2 += ps;
  a->wsum2 += ws;
  return j + 1;
}

/* ======================================================================
                                  wmultiply
   ====================================================================== */

static void wadd(allinfo *a, item *t) {
  register wstate *i, *j, *k;
  register stype c, w, iw, jw, kw;
  register item *mod;
  wstate *r;

  /* initialize limits */
  if (2 * a->db.size + 1 > SSSTATES)
    printf("no space in wadd");
  r = (a->db.fset == a->sstatesb1 ? a->sstatesb2 : a->sstatesb1);
  i = a->db.fset;
  j = a->db.fset;
  k = r;
  w = t->w;
  c = a->wcap[a->k];
  mod = t;

  /* add large state at end, and copy first state */
  (a->db.lset + 1)->wsum = c + 1;
  *k = *i;
  i++;

  /* now merge sets: i, j indices to each set, k index to merged set */
  for (iw = i->wsum, jw = j->wsum + w, kw = k->wsum;;) {
    if (iw <= jw) {
      if (iw > c)
        break;
      k++;
      kw = iw;
      *k = *i;
      i++;
      iw = i->wsum;
    } else {
      if (jw != kw) {
        k++;
        kw = k->wsum = jw;
        k->mod = mod;
      }
      j++;
      jw = j->wsum + w;
    }
  }

  /* save limits */
  a->db.fset = r;
  a->db.lset = k;
  a->db.size = SIZE(&(a->db));
}

static void wsub(allinfo *a, item *s) {
  register wstate *i, *j, *k;
  register stype c, w, iw, jw, kw;
  register item *mod;
  wstate *r;

  /* initialize limits */
  if (2 * a->da.size + 1 > SSSTATES)
    printf("no space in wsub");
  r = (a->da.fset == a->sstatesa1 ? a->sstatesa2 : a->sstatesa1);
  i = a->da.fset;
  j = a->da.fset;
  k = r;
  w = s->w;
  c = a->wcap[a->k] - (a->wsuma2 - a->wsumbw2);
  if (c < 0)
    c = 0; /* never subtract more than can be added */
  mod = s;

  /* add small state at end, and copy first state */
  (a->da.lset + 1)->wsum = c - 1;
  *k = *i;
  i++;

  /* now merge sets: i, j indices to each set, k index to merged set */
  for (iw = i->wsum, jw = j->wsum - w, kw = k->wsum;;) {
    if (iw >= jw) {
      if (iw < c)
        break;
      k++;
      kw = iw;
      *k = *i;
      i++;
      iw = i->wsum;
    } else {
      if (jw != kw) {
        k++;
        kw = k->wsum = jw;
        k->mod = mod;
      }
      j++;
      jw = j->wsum - w;
    }
  }

  /* save limits */
  a->da.fset = r;
  a->da.lset = k;
  a->da.size = SIZE(&(a->da));
}

/* ======================================================================
                              definesolution2
   ====================================================================== */

static void definesolution2(allinfo *a, wstate *i1, wstate *j1) {
  register item *i, *m, *l;
  register wstate *j, *k;
  register stype ps, ws, w0, c;
  mtype y, m1;

  m1 = a->k + 1;
  y = -m1;
  ps = 0;
  c = a->wcap[a->k];

  /* first take items up to b2 */
  for (i = a->fitem2, m = a->b2; i != m; i++) {
    if (m1 >= i->a) {
      i->y = y;
      ps += i->p;
    }
  }

  /* backtrack set da */
  for (j = i1, ws = i1->wsum, w0 = a->wsumbw2; ws != w0; j--) {
    if (j->wsum == ws) {
      i = j->mod;
      i->y = 1;
      ws += i->w;
      ps -= i->p;
    }
  }

  /* backtrack set db */
  for (j = j1, ws = j1->wsum; ws != 0; j--) {
    if (j->wsum == ws) {
      i = j->mod;
      i->y = y;
      ws -= i->w;
      ps += i->p;
    }
  }

  /* sum totals */
  ws = i1->wsum + j1->wsum;
  a->psum2 += ps;
  a->wsum2 += ws;
  a->wsuma2 -= ws;

  /* find new fitem */
  a->fitem2 = compress(a->fitem2, a->t2 - 1, y) + 1;
}

/* ======================================================================
                                  reducewgtset
   ====================================================================== */

static void reducewgtset(allinfo *a, boolean update) {
  register wstate *i, *j, *k, *l;
  register stype ws, iw, kw, c, maxw;
  wstate *i1, *k1;
  int com;

  /* check if knapsack a->k is filled */
  c = a->wcap[a->k];

  i = a->da.fset;
  j = a->da.lset + 1;
  k = a->db.fset;
  l = a->db.lset + 1;
  for (maxw = -1, l->wsum = c + 1, kw = k->wsum; i != j; i++) {
    iw = i->wsum;
    ws = iw + kw;
    while (ws <= c) {
      if (ws > maxw) {
        maxw = ws;
        i1 = i;
        k1 = k;
      }
      k++;
      kw = k->wsum;
      ws = iw + kw;
    }
  }

  /* now see if improved (optimal) solution */
  com = MORE;
  if ((update) && (a->k == a->m - 1) && (a->wsuma2 <= c))
    com = LAST;
  if ((a->s2 < a->fitem2) && (a->t2 > a->litem2))
    com = ALLENUM;
  if (maxw == c)
    com = FILLED;

  if (com != MORE) {
    if (update)
      definesolution2(a, i1, k1);
    a->wcap[a->k] = maxw;
    a->finish2 = TRUE;
  }
}

/* ======================================================================
                                 findwbreak
   ====================================================================== */

static void findwbreak(allinfo *a, mtype m, boolean update) {
  register item *i, *j;
  register stype c, c1;
  stype wsum;
  wstate *k;

  /* find break item for the current knapsack */
  c = c1 = a->wcap[a->k];
  for (i = a->fitem2, j = a->litem2 + 1; i != j; i++) {
    if (m >= i->a) {
      if (i->w > c)
        break;
      else
        c -= i->w;
    }
  }
  wsum = c1 - c;

  /* initialize limits */
  a->b2 = i;
  a->s2 = i - 1;
  a->t2 = i;
  a->wsumbw2 = wsum;

  /* initialize table of partial vectors */
  a->da.size = 1;
  a->da.fset = a->sstatesa1;
  a->da.lset = a->da.fset;

  a->db.size = 1;
  a->db.fset = a->sstatesb1;
  a->db.lset = a->db.fset;

  /* initialize first partial vector */
  k = a->da.fset;
  k->wsum = wsum;
  k->mod = NULL;
  k = a->db.fset;
  k->wsum = 0;
  k->mod = NULL;
}

/* ======================================================================
                                  partition
   ====================================================================== */

static void partition(allinfo *a, boolean update) {
  stype c;
  mtype m;
  item *i;

  c = a->wcap[a->k];
  m = a->k + 1;
  if (c > 0) {
    a->finish2 = FALSE;
    findwbreak(a, m, update);
    for (;;) {
      reducewgtset(a, update);
      if (a->finish2)
        break;

      i = a->t2;
      if (i <= a->litem2) {
        if (m >= i->a)
          wadd(a, i);
        a->t2++;
      }

      i = a->s2;
      if (i >= a->fitem2) {
        if (m >= i->a)
          wsub(a, i);
        a->s2--;
      }
    }
  }
}

/* ======================================================================
                                   satisfy
   ====================================================================== */

static void satisfy(allinfo *a, boolean update, stype *c) {
  stype wsum, tight, lb;
  mtype k;
  ntype n;

  for (k = 0, wsum = 0; k != a->m; k++)
    wsum += c[k];

  /* initialize */
  tight = 0;
  a->wsum2 = 0;
  a->psum2 = 0;
  a->wsuma2 = 2 * a->csum + 1;
  a->fitem2 = a->h + 1;
  a->litem2 = a->litem;

  /* copy capacities */
  a->wcap = static_cast<stype *>(palloc(a->m, sizeof(stype)));
  memcpy(a->wcap, c, a->m * sizeof(stype));

  /* prepare items */
  if (update) {
    a->litem2 = compress(a->fitem2, a->litem2, 1);
    a->psum2 = a->psum;
    a->wsuma2 = a->wstar;
    n = a->litem2 - a->fitem2 + 1;
    if (n <= MAXN)
      weightsort(a, a->fitem2, a->litem2);
  }

  /* now fill the knapsacks */
  for (k = 0; k != a->m; k++) {
    if ((c[k] <= MAXCAP) || (update)) {
      a->k = k;
      partition(a, update);
    }
  }

  for (k = 0; k != a->m; k++) {
    if (update) {
      if ((a->wcap[k] < c[k]) && (a->wsuma2 > 0)) {
        /* solve 0-1 Knapsack Problem by freeing variables, sum, define */
        a->fitem2 = freeitems(a, &lb, a->h + 1, a->litem, k);
        minknap(a, a->fitem2, c[k], lb);
        a->fitem2 = defitems(a, a->fitem2, a->litem, -(k + 1));
      }
    } else {
      tight += c[k] - a->wcap[k];
      a->wsum2 += a->wcap[k];
      c[k] = a->wcap[k];
    }
  }

  if (!update) {
  }
  pfree(a->wcap);
}

/* **********************************************************************
   **********************************************************************
                             0-1 Knapsack Problem
   **********************************************************************
   ********************************************************************** */

/* ======================================================================
                                improvesolution
   ====================================================================== */

static void improvesolution(allinfo *a, state *v) {
  if (v->wsum > a->c)
    printf("wrong improvesoluton");
  if (v->psum < a->z)
    printf("not improved solution");

  a->z = v->psum;
  a->zwsum = v->wsum;
  a->ovect = v->vect;
  memcpy(a->ovitem, a->vitem, sizeof(item *) * MAXV);
}

/* ======================================================================
                                definesolution
   ====================================================================== */

static void definesolution(allinfo *a) {
  register item *i, *m;
  item *f, *l;
  stype psum, wsum;
  btype j, k;

  if (a->firsttime) {
    if (a->z == a->lb) {
      a->welldef = TRUE;
      return;
    }
    a->zstar = a->z;
    a->wstar = a->zwsum;
    for (i = a->h + 1, m = a->b; i != m; i++)
      i->y = 1;
    for (i = a->b, m = a->litem + 1; i != m; i++)
      i->y = 0;
    a->firsttime = FALSE;
  }

  psum = a->z;
  wsum = a->zwsum;
  f = a->fsort - 1;
  l = a->lsort + 1;

  for (j = 0; j < MAXV; j++) {
    k = a->ovect & ((btype)1 << j);
    i = a->ovitem[j];
    if (i == NULL)
      continue;
    if (i->y == 1) {
      if (i > f)
        f = i;
      if (k) {
        psum += i->p;
        wsum += i->w;
        i->y = 0;
      }
    } else {
      if (i < l)
        l = i;
      if (k) {
        psum -= i->p;
        wsum -= i->w;
        i->y = 1;
      }
    }
  }
  a->welldef = (psum == a->psumb) && (wsum == a->wsumb);

  /* prepare for next round */
  if (!a->welldef) {
    a->fsort = f + 1;
    a->lsort = l - 1;
    a->intv1 = a->intv1b;
    a->intv2 = a->intv2b;

    a->c = wsum;
    a->z = psum - 1;
    a->ub = psum;
  }
}

/* ======================================================================
                                partsort
   ====================================================================== */

static void partsort(allinfo *a, item *f, item *l, stype ws, int what) {
  register itype mp, mw;
  register item *i, *j, *m;
  register stype wi;
  int d;

  d = l - f + 1;
  if (d > 1) {
    m = f + d / 2;
    if (FDET(f->p, f->w, m->p, m->w) < 0)
      SWAP(f, m);
    if (d > 2) {
      if (FDET(m->p, m->w, l->p, l->w) < 0) {
        SWAP(m, l);
        if (FDET(f->p, f->w, m->p, m->w) < 0)
          SWAP(f, m);
      }
    }
  }

  if (d > 3) {
    mp = m->p;
    mw = m->w;
    i = f;
    j = l;
    wi = ws;
    for (;;) {
      do {
        wi += i->w;
        i++;
      } while (FDET(i->p, i->w, mp, mw) > 0);
      do {
        j--;
      } while (FDET(j->p, j->w, mp, mw) < 0);
      if (i > j)
        break;
      SWAP(i, j);
    }

    if (wi <= a->cstar) {
      if (what == SORTALL)
        partsort(a, f, i - 1, ws, what);
      if (what == PARTIATE)
        push(a, LEFT, f, i - 1);
      partsort(a, i, l, wi, what);
    } else {
      if (what == SORTALL)
        partsort(a, i, l, wi, what);
      if (what == PARTIATE)
        push(a, RIGHT, i, l);
      partsort(a, f, i - 1, ws, what);
    }
  }

  if ((d <= 3) || (what == SORTALL)) {
    a->fpart = f;
    a->lpart = l;
    a->wfpart = ws;
  }
}

/* ======================================================================
                                  haschance
   ====================================================================== */

static boolean haschance(allinfo *a, item *i, int side) {
  register state *j, *m;
  register ptype p, w, r;
  stype pp, ww;

  if (a->d.size == 0)
    return FALSE;

  if (side == RIGHT) {
    if (a->d.fset->wsum <= a->c - i->w)
      return TRUE;
    p = a->ps;
    w = a->ws;
    pp = i->p - a->z - 1;
    ww = i->w - a->c;
    r = -DET(pp, ww, p, w);
    for (j = a->d.fset, m = a->d.lset + 1; j != m; j++) {
      if (DET(j->psum, j->wsum, p, w) >= r)
        return TRUE;
    }
  } else {
    if (a->d.lset->wsum > a->c + i->w)
      return TRUE;
    p = a->pt;
    w = a->wt;
    pp = -i->p - a->z - 1;
    ww = -i->w - a->c;
    r = -DET(pp, ww, p, w);
    for (j = a->d.lset, m = a->d.fset - 1; j != m; j--) {
      if (DET(j->psum, j->wsum, p, w) >= r)
        return TRUE;
    }
  }
  return FALSE;
}

/* ======================================================================
                                  multiply
   ====================================================================== */

static void multiply(allinfo *a, item *h, int side) {
  register state *i, *j, *k, *m;
  register itype p, w;
  register btype mask0, mask1;
  state *r1, *rm;

  if (a->d.size == 0)
    return;
  if (side == RIGHT) {
    p = h->p;
    w = h->w;
  } else {
    p = -h->p;
    w = -h->w;
  }
  if (2 * a->d.size + 2 > MAXSTATES)
    printf("no space in multiply");

  /* keep track on solution vector */
  a->vno++;
  if (a->vno == MAXV)
    a->vno = 0;
  mask1 = ((btype)1 << a->vno);
  mask0 = ~mask1;
  a->vitem[a->vno] = h;

  /* initialize limits */
  r1 = a->d.fset;
  rm = a->d.lset;
  k = a->d.set1;
  m = rm + 1;
  k->psum = -1;
  k->wsum = r1->wsum + abs(p) + 1;
  m->wsum = rm->wsum + abs(w) + 1;

  for (i = r1, j = r1; (i != m) || (j != m);) {
    if (i->wsum <= j->wsum + w) {
      if (i->psum > k->psum) {
        if (i->wsum > k->wsum)
          k++;
        k->psum = i->psum;
        k->wsum = i->wsum;
        k->vect = i->vect & mask0;
      }
      i++;
    } else {
      if (j->psum + p > k->psum) {
        if (j->wsum + w > k->wsum)
          k++;
        k->psum = j->psum + p;
        k->wsum = j->wsum + w;
        k->vect = j->vect | mask1;
      }
      j++;
    }
  }

  a->d.fset = a->d.set1;
  a->d.lset = k;
  a->d.size = a->d.lset - a->d.fset + 1;
}

/* =========================================================================
                                   simpreduce
   ========================================================================= */

static void simpreduce(int side, item **f, item **l, allinfo *a) {
  register item *i, *j, *k;
  register ptype pb, wb;
  register ptype q, r;

  if (a->d.size == 0) {
    *f = *l + 1;
    return;
  }
  if (*l < *f)
    return;

  pb = a->b->p;
  wb = a->b->w;
  q = DET(a->z + 1 - a->psumb, a->c - a->wsumb, pb, wb);
  r = -DET(a->z + 1 - a->psumb, a->c - a->wsumb, pb, wb);
  i = *f;
  j = *l;
  if (side == LEFT) {
    k = a->fsort - 1;
    while (i <= j) {
      if (DET(j->p, j->w, pb, wb) > r) {
        SWAP(i, j);
        i++; /* not feasible */
      } else {
        SWAP(j, k);
        j--;
        k--; /* feasible */
      }
    }
    *l = a->fsort - 1;
    *f = k + 1;
  } else {
    k = a->lsort + 1;
    while (i <= j) {
      if (DET(i->p, i->w, pb, wb) < q) {
        SWAP(i, j);
        j--; /* not feasible */
      } else {
        SWAP(i, k);
        i++;
        k++; /* feasible */
      }
    }
    *f = a->lsort + 1;
    *l = k - 1;
  }
}

/* ======================================================================
                                  reduceset
   ====================================================================== */

static void reduceset(allinfo *a) {
  register state *i, *m, *k;
  register ptype ps, ws, pt, wt, r;
  stype z, c;
  state *r1, *rm, *v;
  item *f, *l;

  if (a->d.size == 0)
    return;

  /* initialize limits */
  r1 = a->d.fset;
  rm = a->d.lset;

  v = findvect(a->c, r1, rm);
  if (v == NULL) {
    v = r1 - 1; /* all excess weight */
  } else {
    if ((v->psum == a->z) && (v->wsum < a->zwsum))
      improvesolution(a, v);
    if (v->psum > a->z)
      improvesolution(a, v);
  }
  c = a->c;
  z = a->z + 1;
  k = a->d.setm;

  /* expand core, and choose ps, ws */
  if (a->s < a->fsort) {
    if (a->intv1 == a->intv1b) {
      ps = PMAX;
      ws = WMAX;
    } else {
      pop(a, LEFT, &f, &l);
      if (f < a->ftouch)
        a->ftouch = f;
      ps = f->p;
      ws = f->w; /* default: choose item at random */
      simpreduce(LEFT, &f, &l, a);
      if (f <= l) {
        partsort(a, f, l, 0, SORTALL);
        a->fsort = f;
        ps = a->s->p;
        ws = a->s->w;
      }
    }
  } else {
    ps = a->s->p;
    ws = a->s->w;
  }

  /* expand core, and choose pt, wt */
  if (a->t > a->lsort) {
    if (a->intv2 == a->intv2b) {
      pt = PMIN;
      wt = WMIN;
    } else {
      pop(a, RIGHT, &f, &l);
      if (l > a->ltouch)
        a->ltouch = l;
      pt = l->p;
      wt = l->w; /* default: choose item at random */
      simpreduce(RIGHT, &f, &l, a);
      if (f <= l) {
        partsort(a, f, l, 0, SORTALL);
        a->lsort = l;
        pt = a->t->p;
        wt = a->t->w;
      }
    }
  } else {
    pt = a->t->p;
    wt = a->t->w;
  }

  /* now do the reduction */
  r = DET(z, c, ps, ws);
  for (i = rm, m = v; i != m; i--) {
    if (DET(i->psum, i->wsum, ps, ws) >= r) {
      k--;
      *k = *i;
    }
  }

  r = DET(z, c, pt, wt);
  for (i = v, m = r1 - 1; i != m; i--) {
    if (DET(i->psum, i->wsum, pt, wt) >= r) {
      k--;
      *k = *i;
    }
  }
  a->ps = ps;
  a->ws = ws;
  a->pt = pt;
  a->wt = wt;
  a->d.fset = k;
  a->d.lset = a->d.setm - 1; /* reserve one record for multiplication */
  a->d.size = a->d.lset - a->d.fset + 1;
}

/* ======================================================================
                                  initfirst
   ====================================================================== */

static void initfirst(allinfo *a, stype ps, stype ws) {
  state *k;

  a->d.size = 1;
  a->d.set1 = a->kpstates;
  a->d.setm = a->d.set1 + MAXSTATES - 1;
  a->d.fset = a->d.set1;
  a->d.lset = a->d.set1;

  k = a->d.fset;
  k->psum = ps;
  k->wsum = ws;
  k->vect = 0;
}

/* ======================================================================
                                  initvect
   ====================================================================== */

static void initvect(allinfo *a) {
  register btype i;

  for (i = 0; i < MAXV; i++)
    a->vitem[i] = NULL;
  a->vno = MAXV - 1;
}

/* ======================================================================
                                findbreak
   ====================================================================== */

static void findbreak(allinfo *a) {
  register item *i, *m;
  register stype psum, wsum, c;

  psum = 0;
  wsum = 0;
  c = a->cstar;
  for (i = a->h + 1, m = a->litem + 1;; i++) {
    if (i == m)
      break;
    if (wsum + i->w > c)
      break;
    psum += i->p;
    wsum += i->w;
  }

  a->fsort = a->fpart;
  a->lsort = a->lpart;
  a->ftouch = a->fpart;
  a->ltouch = a->lpart;
  a->b = i;
  a->psumb = psum;
  a->wsumb = wsum;
  a->dantzig = (i == m ? psum : psum + ((c - wsum) * (stype)i->p) / i->w);

  /* find greedy solution */
  for (i = a->b, m = a->litem + 1; i != m; i++) {
    if (wsum + i->w <= c) {
      psum += i->p;
      wsum += i->w;
    }
  }

  if (psum > a->lb) {
    for (i = a->h + 1, m = a->b; i != m; i++)
      i->y = 1;
    for (i = a->b, m = a->litem + 1, wsum = a->wsumb; i != m; i++) {
      i->y = 0;
      if (wsum + i->w <= c) {
        wsum += i->w;
        i->y = 1;
      }
    }
    a->lb = psum;
    a->z = psum;
    a->zstar = psum;
    a->wstar = wsum;
  } else {
    a->z = a->lb;
    a->zstar = a->lb;
    a->wstar = a->cstar;
  }
  a->c = a->cstar;
}

/* ======================================================================
                                minknap
   ====================================================================== */

static void minknap(allinfo *a, item *fitem, stype c, stype lb) {
  interval *inttab;
  item *h;

  h = a->h;
  a->h = fitem - 1;

  a->cstar = c;
  a->lb = lb;
  inttab = a->interv;
  a->intv1 = a->intv1b = &inttab[0];
  a->intv2 = a->intv2b = &inttab[SORTSTACK - 1];
  a->fsort = a->litem;
  a->lsort = a->h + 1;
  partsort(a, a->h + 1, a->litem, 0, PARTIATE);
  findbreak(a);

  a->ub = a->dantzig;
  a->firsttime = TRUE;

  for (;;) {
    a->s = a->b - 1;
    a->t = a->b;
    initfirst(a, a->psumb, a->wsumb);
    initvect(a);
    reduceset(a);

    while ((a->d.size > 0) && (a->z != a->ub)) {
      if (a->t <= a->lsort) {
        if (haschance(a, a->t, RIGHT))
          multiply(a, a->t, RIGHT);
        (a->t)++;
      }
      reduceset(a);

      if (a->s >= a->fsort) {
        if (haschance(a, a->s, LEFT))
          multiply(a, a->s, LEFT);
        (a->s)--;
      }
      reduceset(a);
    }
    definesolution(a);
    if (a->welldef)
      break;
  }

  a->h = h;
}

/* **********************************************************************
   **********************************************************************
                           Multiple Knapsack Problem
   **********************************************************************
   ********************************************************************** */

/* ======================================================================
                                 findbest
   ====================================================================== */

static void findbest(allinfo *a, stype *c, item **k1, mtype *i1) {
  register item *j, *k, *m;
  mtype i;

  *k1 = NULL;
  *i1 = 0;
  if (a->h == a->litem)
    return;
  for (i = 0; i < a->m; i++) {
    if (c[i] > 0) {
      /* remove items, not allowed for this knapsack */
      for (j = k = a->h + 1, m = a->litem + 1; j != m; j++) {
        if (j->a > i + 1) {
          SWAP(j, k);
          k++;
        }
      }

      /* solve the knapsack problem */
      minknap(a, k, c[i], 0);

      /* find most efficient item */
      for (j = k, m = a->litem + 1; j != m; j++)
        if (j->y == 1)
          break;
      if (j != m) {
        k = j;
        for (j = k + 1; j != m; j++) {
          if (j->y == 1) {
            if (DET(j->p, j->w, k->p, k->w) > 0)
              k = j;
          }
        }
        if (k->a > i + 1)
          printf("bad choice");
        *k1 = k;
        *i1 = i;
        return;
      }
    }
  }
  /* no items fit into any knapsacks */
}

/* ======================================================================
                               copysolution
   ====================================================================== */

static void copysolution(allinfo *a) {
  register item *k, *l;

  a->z1 = a->psum2;
  for (k = a->fitem, l = a->litem + 1; k != l; k++) {
    if (k->y < 0)
      k->x = -k->y;
    else
      k->x = 0;
  }
}

/* ======================================================================
                               reduceitems
   ====================================================================== */

static void reduceitems(allinfo *a, stype z, stype c) {
  register item *i, *j, *k;
  register itype pb, wb;
  register stype q;

  pb = a->brp;
  wb = a->brw;
  q = DET(z + 1 - a->psumb2, c - a->wsumb2, pb, wb);
  i = a->h + 1;
  j = a->litem;
  k = a->h + 1;
  while (i <= j) {
    if (DET(i->p, i->w, pb, wb) < q) {
      i->y = 0;
      SWAP(i, k);
      i++;
      k++; /* not feasible */
      a->curred++;
    } else {
      SWAP(i, j);
      j--; /* feasible */
    }
  }
  a->h = k - 1;
}

/* ======================================================================
                                alloctables
   ====================================================================== */

static void alloctables(allinfo *a) {
  a->kpstates = static_cast<state *>(palloc(MAXSTATES, sizeof(state)));
  a->sstatesa1 = static_cast<wstate *>(palloc(SSSTATES, sizeof(wstate)));
  a->sstatesa2 = static_cast<wstate *>(palloc(SSSTATES, sizeof(wstate)));
  a->sstatesb1 = static_cast<wstate *>(palloc(SSSTATES, sizeof(wstate)));
  a->sstatesb2 = static_cast<wstate *>(palloc(SSSTATES, sizeof(wstate)));
  a->interv = static_cast<interval *>(palloc(SORTSTACK, sizeof(interval)));
}

static void freetables(allinfo *a) {
  pfree(a->kpstates);
  pfree(a->sstatesa1);
  pfree(a->sstatesa2);
  pfree(a->sstatesb1);
  pfree(a->sstatesb2);
  pfree(a->interv);
}

/* ======================================================================
                                mulbranch
   ====================================================================== */

static void mulbranch(allinfo *a, stype *c1) {
  item *j, *h;
  mtype i, a1;
  stype *c;
  stype csum, ub;
  itype p, w;

  /* copy capacities, and tighten them */
  c = static_cast<stype *>(palloc(a->m, sizeof(stype)));
  memcpy(c, c1, a->m * sizeof(stype));
  satisfy(a, FALSE, c);
  csum = a->wsum2;
  /* csum = tighten(a, c); */

  /* determine upper and lower bound */
  minknap(a, a->h + 1, csum, a->z1 - a->psum);
  ub = a->zstar + a->psum;
  a->brp = a->b->p;
  a->brw = a->b->w;
  a->psumb2 = a->psumb + a->psum;
  a->wsumb2 = a->wsumb; /* weight of fixed items are subtracted from c */
  if (ub > a->z1) {
    satisfy(a, TRUE, c);
    if (a->psum2 > a->z1)
      copysolution(a);
  }

  /* check upper bound */
  if ((ub > a->z1) && (a->h < a->litem) && (csum > 0)) {
    h = a->h; /* save h for freeing reduced items */
    reduceitems(a, a->z1, csum);
    findbest(a, c, &j, &i);
    if ((a->h < a->litem) && (j != NULL)) {
      a->h++;
      SWAP(j, a->h);
      j = a->h;
      if (j > a->maxh)
        a->maxh = j;

      /* put item j in knapsack i */
      c[i] -= j->w;
      a->psum += j->p;
      a->wsum += j->w;
      j->y = -(i + 1);
      if (c[i] >= 0)
        mulbranch(a, c);
      c[i] += j->w;
      a->psum -= j->p;
      a->wsum -= j->w;
      a->h--;

      /* exclude item j from knapsack i */
      a1 = j->a;
      j->a = i + 2;
      j->y = 0;
      p = j->p;
      w = j->w;
      mulbranch(a, c);
      for (j = a->h + 1; j != a->litem + 1; j++) {
        if ((j->p == p) && (j->w == w) && (j->a == i + 2))
          break;
      }
      if (j == a->litem + 1)
        printf("cannot find %hd,%hd", p, w);
      j->a = a1;
    }
    a->curred -= (a->h - h);
    a->h = h; /* free reduced items */
  }

  /* free and return */
  pfree(c);
}

/* ======================================================================
                                mulknap
   ====================================================================== */

static int mulknap(int n, int m, int *p, int64_t *w, int *x, int64_t *c) {
  register ntype i, l;
  register item *j, *k;
  allinfo a;
  item *tab;
  interval *inttab;
  long t1, t2;

  /* initialize debug variables, and start timer */
  give_time(&t1);

  /* copy variables to internal structure */
  a.n = n;
  a.m = m;
  a.curred = 0;

  /* allocate space for test example and two border items */
  tab = (item *)palloc(n + 1, sizeof(item));
  a.fitem = &tab[1];
  a.litem = &tab[n];

  /* copy test instance */
  a.cap = static_cast<stype *>(palloc(a.m, sizeof(stype)));
  a.csum = 0;
  for (i = 1; i != m; i++) {
    if (c[i - 1] > c[i])
      printf("c not ordered");
  }
  for (i = 0; i != m; i++) {
    a.cap[i] = c[i];
    a.csum += c[i];
  }
  for (i = 0, l = n, j = a.fitem; i != l; i++, j++) {
    j->x = -1;
    j->y = 0;
    j->a = 1;
    j->w = w[i];
    j->p = p[i];
    j->xp = x + i;
  }

  /* now the branch-and-bound part */
  a.z1 = 0;
  a.psum = 0;
  a.wsum = 0;
  a.h = a.fitem - 1;
  a.maxh = a.h;
  alloctables(&a);

  /* run the branch-and-bound algorithm */
  mulbranch(&a, a.cap);

  /* define solution vector */
  for (j = a.fitem, k = a.litem + 1; j != k; j++)
    *(j->xp) = j->x;

  /* free global tables */
  freetables(&a);
  pfree(a.cap);
  pfree(tab);

  /* stop timer and return */
  give_time(&t2);
  return a.z1;
}