/* pssm_asn_subs.c */

/*  $Id: pssm_asn_subs.c 27 2008-06-30 16:27:31Z pearson $ */
/* $Revision: 28 $  */

/* copyright (C) 2005 by William R. Pearson and the U. of Virginia */

/* read_asn_dest modified 26-Jul-2007 to skip over text/bytes if dest is NULL */

/* this code is designed to parse the ASN.1 binary encoded scoremat
   object produced by blastpgp -C file.ckpt_asn -u 2 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"

int parse_pssm_asn();
int parse_pssm2_asn();

int
parse_pssm_asn_fa(FILE *afd, int *n_rows, int *n_cols,
		  unsigned char **query, double ***freqs,
		  char *matrix, int *gap_open, int *gap_extend,
		  double *lambda);



#define COMPO_NUM_TRUE_AA 20

/**positions of true characters in protein alphabet*/
/*
static int trueCharPositions[COMPO_NUM_TRUE_AA] = {
  1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,22
};
*/

#define COMPO_LARGEST_ALPHABET 28

/*
static char ncbieaatoa[COMPO_LARGEST_ALPHABET] = {"-ABCDEFGHIJKLMNOPQRSTUVWXYZ"};

static int alphaConvert[COMPO_LARGEST_ALPHABET] = {
  (-1), 0, (-1), 4, 3, 6, 13, 7, 8, 9, 11, 10, 12, 2, 14, 5, 1, 15,
  16, 19,   17, (-1), 18, (-1), (-1), (-1), (-1), (-1)
};
*/

int pssm_aa_order[20] = { 1,  /*A*/
			  16, /*R*/
			  13, /*N*/
			   4, /*D*/
			   3, /*C*/
			  15, /*Q*/
			   5, /*E*/
			   7, /*G*/
			   8, /*H*/
			   9, /*I*/
			  11, /*L*/
			  10, /*K*/
			  12, /*M*/
			   6, /*F*/
			  14, /*P*/
			  17, /*S*/
			  18, /*T*/
			  20, /*W*/
			  22, /*Y*/
			  19}; /*V*/

#define ABP *asnp->abp
#define ABPP asnp->abp
#define ABP_INC2 asnp->abp += 2

#define ASN_SEQ 48
#define ASN_SET 48
#define ASN_SEQOF 49
#define ASN_SETOF 49

#define ASN_PSSM_QUERY 166
#define ASN_PSSM2_VERSION 160
#define ASN_PSSM2_QUERY 161
#define ASN_PSSM2_MATRIX 162

#define ASN_PSSM_IS_PROT 160
#define ASN_PSSM_NROWS 162
#define ASN_PSSM_NCOLS 163

#define ASN_PSSM_BYCOL 165
#define ASN_PSSM_INTERMED_DATA 167
#define ASN_PSSM_FREQS 162

#define ASN_PSSM2_IS_PROTEIN 160
#define ASN_PSSM2_MATRIX_NAME 161
#define ASN_PSSM2_MATRIX_COMMENT 162	/* not used */
#define ASN_PSSM2_NCOLS 163
#define ASN_PSSM2_NROWS 164
#define ASN_PSSM2_SCORES 165
#define ASN_PSSM2_KARLIN_K 166
#define ASN_PSSM2_FREQS 167

#define ASN_IS_STR 26
#define ASN_IS_SSTR 65
#define ASN_IS_INT  2
#define ASN_IS_BOOL 1
#define ASN_IS_OCTSTR 4
#define ASN_IS_OCTSSTR 65
#define ASN_IS_REAL 0
#define ASN_IS_ENUM 10
#define ASN_IS_ENUM0 1

#define ASN_OBJ_INT 160
#define ASN_OBJ_STR 161

struct asn_bstruct {
  FILE *fd;
  unsigned char *buf;
  unsigned char *abp;
  unsigned char *buf_max;
  int len;
};

#define ASN_BUF 4096

void *
new_asn_bstruct(int buf_siz) {

  struct asn_bstruct *asnp;

  if ((asnp=calloc(1,sizeof(struct asn_bstruct)))==NULL) {
    fprintf(stderr, "cannot allocate asn_bstruct\n");
    exit(1);
  }

  if ((asnp->buf = (unsigned char *)calloc(buf_siz, sizeof(char))) == NULL ) {
    fprintf(stderr, " cannot allocate asn_buf (%d)\n",buf_siz);
    exit(1);
  }

  return asnp;
}

void
free_asn_bstruct(struct asn_bstruct *asnp) {

  if (asnp == NULL) return;
  if (asnp->buf != NULL) free(asnp->buf);
  free(asnp);
}

unsigned char *
chk_asn_buf(struct asn_bstruct *asnp, int v) {
  int new_buf;
  
  if (v > ASN_BUF) {
    fprintf(stderr," attempt to read %d bytes ASN.1 data > buffer size (%d)\n",
	    v, ASN_BUF);
    exit(1);
  }

  if (asnp->abp + v > asnp->buf_max) {

    /* move down the left over stuff */
    asnp->len = asnp->buf_max - asnp->abp;

    memmove(asnp->buf, asnp->abp, asnp->len);

    asnp->abp = asnp->buf;
    new_buf = ASN_BUF - asnp->len;
    
    if (asnp->fd && !feof(asnp->fd) && 
	(new_buf=fread(asnp->buf + asnp->len, sizeof(char), new_buf, asnp->fd)) != 0) {
      asnp->len += new_buf;
    }

    asnp->buf_max = asnp->buf + asnp->len;

    if (asnp->len < v) {
      fprintf(stderr, " Unable to read %d bytes\n",v);
      exit(1);
    }
  }
  /* otherwise, v bytes are currently in the buffer */

  return asnp->abp;
}

unsigned char *
asn_error(char *func, char *token, int tval, 
	  struct asn_bstruct *asnp, int len) {
  int i;

  fprintf(stderr," %s %s [%0x]:",func, token, tval);
  for (i=0; i<len; i++) {
    fprintf(stderr," %0x",asnp->abp[i]);
  }
  fprintf(stderr,"\n");
  return asnp->abp;
}


/* 
   read_asn_dest reads v bytes into oct_str if v <= o_len - otherwise
   fails - the correct size buffer must be pre-allocated read_asn_dest
   is required for ASN data entities that are longer than ASN_BUF
   (1024)
   
   skip over if oct_str==NULL;
*/
unsigned char *
read_asn_dest(struct asn_bstruct *asnp, int v, unsigned char *oct_str, int o_len) {
  int new_buf;
  unsigned char *oct_ptr;
  

  if (oct_str != NULL && v > o_len) {
    fprintf(stderr, " read_asn_dest - cannot read %d bytes into %d buffer\n",
	    v, o_len);
    exit(1);
  }

  if (asnp->abp + v <= asnp->buf_max) {
    if (oct_str != NULL) memmove(oct_str, asnp->abp, v);
    return asnp->abp+v;
  }
  else {
    /* move down the left over stuff */

    asnp->len = asnp->buf_max - asnp->abp;

    if (oct_str != NULL)  memmove(oct_str, asnp->abp, asnp->len);
    oct_ptr = oct_str+asnp->len;
    v -= asnp->len;

    asnp->abp = asnp->buf;
    new_buf = ASN_BUF;
    
    while ((new_buf=fread(asnp->buf, sizeof(char), new_buf, asnp->fd)) != 0) {
      asnp->len = new_buf;
      asnp->buf_max = asnp->buf + asnp->len;
      if (v <= new_buf) {	/* we have it all this time */
	if (oct_str != NULL)  memmove(oct_ptr, asnp->buf, v);
	asnp->len -= v;
	asnp->abp = asnp->buf + v;
	break;
      }
      else {	/* we need to read some more */
	if (oct_str != NULL)  memmove(oct_ptr, asnp->buf, new_buf);
	v -= new_buf;
	new_buf = ASN_BUF;
      }
    }
  }
  return asnp->buf + v;
}

unsigned char *
get_astr_bool(struct asn_bstruct *asnp, int *val) {

  int v_len, v;

  asnp->abp = chk_asn_buf(asnp,5);

  v = 0;
  if (*asnp->abp++ != 1) { /* check for int */
    fprintf(stderr," bool missing\n");
  }
  else {
    v_len = *asnp->abp++;
    if (v_len != 1) {
      fprintf(stderr, "boolean length != 1 : %d\n", v_len);
      v = *asnp->abp++;
    }
    else { v = *asnp->abp++;}
  }
  *val = v;
  return asnp->abp;
}

unsigned char *
get_astr_int(struct asn_bstruct *asnp,
	    int *val) {

  int v_len, v;

  v = 0;

  asnp->abp = chk_asn_buf(asnp,8);

  if (*asnp->abp++ != ASN_IS_INT) { /* check for int */
    return asn_error("get_astr_int", "ASN_IS_INT", ASN_IS_INT, asnp, 4);
  }
  else {
    v_len = *asnp->abp++;
    while (v_len-- > 0) {
      v *= 256;
      v += *asnp->abp++;
    }
  }
  *val = v;
  return asnp->abp;
}

unsigned char *
get_astr_real(struct asn_bstruct *asnp,
	    double *val) {

  int v_len, v;
  asnp->abp = chk_asn_buf(asnp,16);

  *val = 0.0;
  if (ABP != '\0') {
    fprintf(stderr," float missing\n");
    return asnp->abp;
  }
  else {
    sscanf((char *)ABPP,"%lg",val);
    while (ABP) { asnp->abp++;}	/* get to EOS */
    asnp->abp++;
  }
  return asnp->abp;
}

unsigned char *
get_astr_enum(struct asn_bstruct *asnp, int *val) {

  int v_len, v;

  asnp->abp = chk_asn_buf(asnp,5);

  v = 0;
  if (*asnp->abp++ != ASN_IS_ENUM) { /* check for int */
    fprintf(stderr," enum missing\n");
  }
  else {
    v_len = *asnp->abp++;
    while (v_len-- > 0) { v *= 256;  v += *asnp->abp++; }
  }
  *val = v;

  return asnp->abp;
}

unsigned char *
get_astr_packedfloat(struct asn_bstruct *asnp, double *val) {

  int v_len, v;
  char tmp_str[64];

  asnp->abp = chk_asn_buf(asnp,2);

  v = 0;
  if (*asnp->abp++ != 9) { /* check for packed float */
    fprintf(stderr," float missing\n");
    *val = 0;
    return asnp->abp;
  }
  else {
    v_len = *asnp->abp++;

    if (v_len > 63) {
      fprintf(stderr," real string too long: %d\n",v_len);
    }

    asnp->abp = chk_asn_buf(asnp,v_len);

    if (v_len == 2  && *asnp->abp == '\0' && *(asnp->abp+1)=='0') {
      ABP_INC2;
      *val = 0.0;
    }
    else {	/* copy and scan it */
      if (*asnp->abp != '\0') {
	fprintf(stderr, " packedfloat - expected 0, got %d\n", *asnp->abp);
	*val = -1.0;
	return asnp->abp;
      }
      asnp->abp++;
      strncpy(tmp_str, (char *)asnp->abp, sizeof(tmp_str)-1);
      tmp_str[v_len-1] = '\0';
      tmp_str[63] = '\0';
      sscanf(tmp_str,"%lg",val);
      asnp->abp += v_len-1;
    }
  }
  return asnp->abp;
}

unsigned char *
get_astr_str(struct asn_bstruct *asnp, char *text, int t_len) {

  int v_len, tv_len;

  asnp->abp = chk_asn_buf(asnp,2);

  if (text != NULL) text[0] = '\0';

  if (ABP != ASN_IS_STR  && ABP != ASN_IS_SSTR) { /* check for str */
    return asn_error("get_astr_str", "ASN_IS_STR", ASN_IS_STR, asnp, 4);
  }
  asnp->abp++;

  v_len = *asnp->abp++;
  if (v_len > 128) { /* need to read the length from the next bytes */
    tv_len = v_len &0x7f;

    asnp->abp = chk_asn_buf(asnp,tv_len);

    for (v_len =0; tv_len; tv_len--) { v_len = (v_len << 8) + *asnp->abp++; }
  }

  /* read v_len bytes */

  if (v_len < t_len) { /* the string fits in the buffer */
    asnp->abp = read_asn_dest(asnp,v_len, (unsigned char *)text, t_len);
  }
  else {	/* it does not fit, fill the buffer and skip */
    if (t_len > 0) 
      asnp->abp = read_asn_dest(asnp,t_len, (unsigned char *)text, t_len);
    asnp->abp = read_asn_dest(asnp,v_len - t_len, NULL, 0);
  }
  if (text != NULL && t_len > 0) {text[min(v_len,t_len)]='\0';}
  return asnp->abp;
}

unsigned char *
get_astr_octstr(struct asn_bstruct *asnp,
	       unsigned char *oct_str,
	       int o_len) {

  int q_len, v_len;

  asnp->abp = chk_asn_buf(asnp,2);

  if (ABP == ASN_IS_OCTSTR || ABP == ASN_IS_OCTSSTR) {
    ABPP++;
    /* get length  of length */
    if (ABP > 128) {
      v_len = *asnp->abp++ & 0x7f;

      asnp->abp = chk_asn_buf(asnp,v_len);

      q_len = 0;
      while (v_len-- > 0) {
	q_len *= 256;
	q_len += *asnp->abp++;
      }
    }
    else {
      q_len = *asnp->abp++ & 0x7f;
    }

    if (q_len < o_len) { /* the string fits in the buffer */
      asnp->abp = read_asn_dest(asnp,q_len, oct_str, o_len);
    }
    else {	/* it does not fit, fill the buffer and skip */
      asnp->abp = read_asn_dest(asnp,o_len, oct_str, o_len);
      asnp->abp = read_asn_dest(asnp,q_len - o_len, NULL, 0);
    }
    if (oct_str != NULL && o_len > 0) oct_str[min(q_len,o_len)]='\0';

    /*    asnp->abp += 2; */	/* skip characters and NULL's */
  }
  return asnp->abp;
}

/* something to try to skip over stuff we don't want */
unsigned char *
get_astr_junk(struct asn_bstruct *asnp) {

  int seq_cnt = 0;
  int tmp;
  char string[256];

  while (ABP) {
    if ( ABP  == ASN_SEQ) { ABP_INC2; seq_cnt++;}
    else if ( ABP == ASN_IS_BOOL ) {
      ABP_INC2;
      ABPP = get_astr_int(asnp, &tmp) + 2;
    }
    else if ( ABP == ASN_IS_INT ) {
      ABP_INC2;
      ABPP = get_astr_int(asnp, &tmp) + 2;
    }
    else if ( ABP == ASN_IS_STR ) {
      ABP_INC2;
      ABPP = get_astr_str(asnp, string, sizeof(string)-1) + 2;
    }
  }

  while (seq_cnt-- > 0) ABP_INC2;
  return asnp->abp;
}

#define ASN_SEQINST_NCBIEAA 167
#define ASN_SEQINST_NCBISTDAA 169

unsigned char *
get_astr_iseqd(struct asn_bstruct *asnp,
	       unsigned char *query,
	       int nq) {

  int q_len, v_len;

  asnp->abp = chk_asn_buf(asnp,2);

  /* check for the sequence type - NCBIstdaa or NCBIstdeaa */

  if (ABP == ASN_SEQINST_NCBIEAA) {
    ABP_INC2;
    return get_astr_str(asnp, (char *)query, nq) + 2;
  }
  else if (ABP == ASN_SEQINST_NCBISTDAA) {
    ABP_INC2;
    return get_astr_octstr(asnp, query, nq) + 2;
  }
  else {
    return asn_error("get_astr_iseqd","",-1,asnp,4);
  }
}

unsigned char *
get_astr_objid(struct asn_bstruct *asnp, int *type, int *val, char *text, int t_len) {

  int local_ival;
  int v_len;

  asnp->abp = chk_asn_buf(asnp,16);

  if (text != NULL) text[0] = '\0';
  if (val != NULL) *val = 0;
  *type = 0;

  /* object could be text, or could be int */

  if (ABP == ASN_OBJ_INT) {
    ABP_INC2;
    asnp->abp = get_astr_int(asnp, &local_ival)+2;
    if (val != NULL) *val = local_ival;
    *type = 1;
  }
  else if (ABP == ASN_OBJ_STR) {
    ABP_INC2;
    asnp->abp = get_astr_str(asnp, text, t_len)+2;
    *type = 2;
  }
  else {
    return asn_error("get_astr_objid","ASN_OBJ_STR",ASN_OBJ_STR,asnp,4);
  }
  return asnp->abp;
}

#define ASN_BIOSEQ_SEQ 160
#define ASN_BIOSEQ_ID  160
#define ASN_BIOSEQ_ID_VAL 160
#define ASN_BIOSEQ_ID_OBJ 160
#define ASN_BIOSEQ_ID_LOCAL 161
#define ASN_BIOSEQ_ID_GIBBSQ 162
#define ASN_BIOSEQ_ID_GIBBMT 163
#define ASN_BIOSEQ_ID_GB 164
#define ASN_BIOSEQ_ID_EMBL 165
#define ASN_BIOSEQ_ID_PIR 166
#define ASN_BIOSEQ_ID_SP 167
#define ASN_BIOSEQ_ID_PATENT 168
#define ASN_BIOSEQ_ID_OTHER 169
#define ASN_BIOSEQ_ID_GEN 170
#define ASN_BIOSEQ_ID_GI 171
#define ASN_BIOSEQ_ID_DDBJ 172
#define ASN_BIOSEQ_ID_PDB 173
#define ASN_BIOSEQ_ID_TPG 174
#define ASN_BIOSEQ_ID_TPE 175
#define ASN_BIOSEQ_ID_TPD 176

#define ASN_BIOSEQ_TEXTID_NAME 160
#define ASN_BIOSEQ_TEXTID_ACC 161
#define ASN_BIOSEQ_TEXTID_REL 162
#define ASN_BIOSEQ_TEXTID_VER 163

#define ASN_BIOSEQ_DESCR  161
#define ASN_BIOSEQ_INST  162
#define ASN_BIOSEQ_D_NAME  163
#define ASN_BIOSEQ_D_TITLE  164
#define ASN_BIOSEQ_D_PIR  169
#define ASN_BIOSEQ_D_GB   170
#define ASN_BIOSEQ_D_USER 173
#define ASN_BIOSEQ_D_SP 174

#define ASN_BIOSEQ_INST_REPR  160
#define ASN_BIOSEQ_INST_MOL  161
#define ASN_BIOSEQ_INST_LEN  162
#define ASN_BIOSEQ_INST_SEQD  166

#define ASN_USERFLD_D_STR 160
#define ASN_USERFLD_D_INT 161
#define ASN_USERFLD_D_REAL 162
#define ASN_USERFLD_D_BOOL 163
#define ASN_USERFLD_D_OS 164
#define ASN_USERFLD_D_USER 165
#define ASN_USERFLD_D_STRS 166
#define ASN_USERFLD_D_INTS 167
#define ASN_USERFLD_D_REALS 168
#define ASN_USERFLD_D_OSS 169
#define ASN_USERFLD_D_FIELDS 170
#define ASN_USERFLD_D_OBJS 171

unsigned char *
get_astr_userfld_data(struct asn_bstruct *asnp) {
  double real;
  int ival;
  int bool;
  
  ABPP = chk_asn_buf(asnp, 16);

  switch (ABP) {
  case ASN_USERFLD_D_STR :
    ABP_INC2;
    ABPP = get_astr_str(asnp, NULL, 0) + 2;
    break;
  case ASN_USERFLD_D_INT :
    ABP_INC2;
    ABPP = get_astr_int(asnp, &ival) + 2;
    break;
  case ASN_USERFLD_D_REAL :
    ABP_INC2;
    ABPP = get_astr_real(asnp, &real) + 2;
    break;
  case ASN_USERFLD_D_BOOL :
    ABP_INC2;
    ABPP = get_astr_bool(asnp, &bool) + 2;
    break;
  case ASN_USERFLD_D_OS :
    ABP_INC2;
    ABPP = get_astr_octstr(asnp, NULL, 0)+2;
    break;
  case ASN_USERFLD_D_OSS :
    asnp->abp += 4;
    ABPP = get_astr_octstr(asnp, NULL, 0)+4;
    break;
  default: 
    return asn_error("get_astr_userfld_data","",0,asnp,4);
  }
  return asnp->abp;
}

#define ASN_USERFLD_LABEL 160
#define ASN_USERFLD_NUM 161
#define ASN_USERFLD_DATA 162

unsigned char *
get_astr_userfld(struct asn_bstruct *asnp) {

  char *func = "get_astr_userfld";
  int num, type, in_seq=0;

  asnp->abp = chk_asn_buf(asnp, 16);

  if (ABP == ASN_SEQ) { in_seq = 1; ABP_INC2;}

  if (ABP != ASN_USERFLD_LABEL) {
    return asn_error(func, "ASN_USERFLD_LABEL", ASN_USERFLD_LABEL, asnp, 4);
  }
  else {
    ABP_INC2;
    asnp->abp = get_astr_objid(asnp, &type, NULL, NULL, 0)+2;
  }
  
  if (ABP == ASN_USERFLD_NUM) {
    asnp->abp +=2;
    asnp->abp = get_astr_int(asnp, &num)+2;
  }

  if (ABP != ASN_USERFLD_DATA) {
    return asn_error(func, "ASN_USERFLD_DATA", ASN_USERFLD_DATA, asnp, 4);
  }
  else {
    ABP_INC2;
    asnp->abp = get_astr_userfld_data(asnp)+2;
  }
  if (in_seq) ABP_INC2;
  return asnp->abp;
}

#define ASN_USER_CLASS 160
#define ASN_USER_TYPE 161
#define ASN_USER_DATA 162

unsigned char *
get_astr_user(struct asn_bstruct *asnp) {
  int type;
  
  char *func = "get_astr_user";
  asnp->abp = chk_asn_buf(asnp,16);

  ABP_INC2;	/* skip SEQ */
  if (ABP == ASN_USER_CLASS) {
    ABP_INC2;
    asnp->abp = get_astr_str(asnp, NULL, 0) + 2;
  }
  if (ABP != ASN_USER_TYPE) {
    return asn_error(func, "ASN_USER_TYPE", ASN_USER_TYPE, asnp, 4);
  }
  else {
    ABP_INC2;
    asnp->abp = get_astr_objid(asnp, &type, NULL, NULL, 0) + 2;
  }

  if (ABP != ASN_USER_DATA) {
    return asn_error(func,"ASN_USER_DATA", ASN_USER_DATA, asnp, 4);
  }
  else {
    asnp->abp += 4;	/* skip over, data, SEQ */
    asnp->abp = chk_asn_buf(asnp,8);
    asnp->abp = get_astr_userfld(asnp);
    asnp->abp += 4;
  }
  return asnp->abp;
}

unsigned char *
get_astr_seqdescr(struct asn_bstruct *asnp,
		 char *descr) {

  int end_seq=0;
  
  /* get seqof '1' */
  /* get 164/128 -  title */
  /* get string */
  /* pop nulls */

  asnp->abp = chk_asn_buf(asnp,6);

  if (ABP == ASN_SEQOF) {
    end_seq++;
    ABP_INC2;
  }
  else {
    fprintf(stderr, " missing ASN_SEQOF '1': %0x %0x\n",ABP, asnp->abp[1]);
  }

  while (ABP != '\0') {

    if (ABP == ASN_BIOSEQ_D_TITLE) { 
      ABP_INC2;	/* skip token */
      asnp->abp = get_astr_str(asnp, descr, MAX_STR) + 2;
    }
    else if (ABP == ASN_BIOSEQ_D_USER) {
      ABP_INC2;
      asnp->abp = get_astr_user(asnp);
    }
    else {
      fprintf(stderr, "Un-parsed Seq-descr: %x %x\n",asnp->abp[0],asnp->abp[1]);
      return asnp->abp;
    }
  }

  asnp->abp = chk_asn_buf(asnp,4);

  if (end_seq) ABP_INC2;

  return asnp->abp;
}

unsigned char *
get_astr_seqinst(struct asn_bstruct *asnp,
		unsigned char **query,
		int *nq) {

  int end_seq=0, tmp;

  /* get sequence '0' */
  /* get 160/128/10/len/val -  repr enum raw val */
  /* get 161/128/10/len/val -  mol enum aa val */
  /* get 162/128/02/len/val -  length int val */
  /* get 166/128 - topology (empty) */
  /* get 167/128 - seq-data */
  /* get 65/len+128/len/octet_string */
  /* pop nulls */

  asnp->abp = chk_asn_buf(asnp,12);

  if (ABP == ASN_SEQ) {
    end_seq++;
    ABP_INC2;
  }
  else {
    fprintf(stderr, " missing ASN_SEQ '0': %0x %0x\n",ABP, asnp->abp[1]);
  }

  if (ABP == ASN_BIOSEQ_INST_REPR && *(asnp->abp+1) == 128) {
    ABP_INC2;
    asnp->abp = get_astr_enum(asnp, &tmp)+2;
  }
  else {
    fprintf(stderr, " missing ASN_BIOSEQ_INST_REPR 160: %0x %0x\n",ABP, asnp->abp[1]);
  }

  if (ABP == ASN_BIOSEQ_INST_MOL && *(asnp->abp+1) == 128) {
    ABP_INC2;
    asnp->abp = get_astr_enum(asnp, &tmp)+2;
  }
  else {
    fprintf(stderr, " missing ASN_BIOSEQ_INST_MOL 161: %0x %0x\n",ABP, asnp->abp[1]);
  }

  if (ABP == ASN_BIOSEQ_INST_LEN) {
    ABP_INC2;
    asnp->abp = get_astr_int(asnp, nq)+2;
  }
  else {
    fprintf(stderr, " missing ASN_BIOSEQ_INST_LEN 161: %0x %0x\n",ABP, asnp->abp[1]);
    return asnp->abp;
  }

  if ((*query = (unsigned char *)calloc(*nq + 1, sizeof(char)))==NULL) {
    fprintf(stderr, " cannot read %d char query\n", *nq+1);
  }

  if (ABP == ASN_BIOSEQ_INST_SEQD) {
    ABP_INC2;
    asnp->abp = get_astr_iseqd(asnp, *query, *nq+1 ) + 2;
  }
  else {
    fprintf(stderr, " missing ASN_BIOSEQ_INST_SEQD 166: %0x %0x\n",ABP, asnp->abp[1]);
    return asnp->abp;
  }

  if (end_seq) ABP_INC2;

  return asnp->abp;
}


unsigned char *
get_astr_textid( struct asn_bstruct *asnp,
		char *name,
		char *acc) {
  int end_seq = 0;
  int ver;
  char this_func[]="get_astr_textid";

  chk_asn_buf(asnp,16);

  if (ABP != ASN_SEQ) {
    fprintf(stderr, " %s - Expected ASN_SEQ: %0x %0x\n",this_func,ABP, asnp->abp[1]);
  }
  else {ABP_INC2; end_seq++;}

  name[0] = acc[0] = '\0';
  
  if (ABP == ASN_BIOSEQ_TEXTID_NAME) {
    ABP_INC2;
    asnp->abp = get_astr_str(asnp, name, MAX_SSTR) + 2;
  }

  if (ABP == ASN_BIOSEQ_TEXTID_ACC) {
    ABP_INC2;
    asnp->abp = get_astr_str(asnp, acc, MAX_SSTR) + 2;
  }

  if (ABP == ASN_BIOSEQ_TEXTID_REL) {
    ABP_INC2;
    asnp->abp = get_astr_str(asnp, NULL, 0) + 2;
  }

  if (ABP == ASN_BIOSEQ_TEXTID_VER) {
    ABP_INC2;
    asnp->abp = get_astr_int(asnp, &ver)+2;
  }

  if (end_seq) ABP_INC2;
  return asnp->abp;
}

unsigned char *
get_astr_seqid (struct asn_bstruct *asnp,
		int *gi,
		char *name,
		char *acc) {
  int type, val;

  *gi = 0;
  acc[0] = '\0';
  while (ABP != '\0') {

    switch (ABP) {
    case ASN_BIOSEQ_ID_OBJ:
      ABP_INC2;
      asnp->abp = get_astr_objid(asnp, &type, &val, name, MAX_SSTR) + 2;
      break;
    case ASN_BIOSEQ_ID_LOCAL:
      ABP_INC2;
      asnp->abp = get_astr_str(asnp, name, MAX_SSTR) + 2;
      break;
    case ASN_BIOSEQ_ID_GI:
      ABP_INC2;
      asnp->abp = get_astr_int(asnp, gi) + 2;
      break;

    case ASN_BIOSEQ_ID_GB:
    case ASN_BIOSEQ_ID_EMBL:
    case ASN_BIOSEQ_ID_PIR:
    case ASN_BIOSEQ_ID_SP:
    case ASN_BIOSEQ_ID_OTHER:
      ABP_INC2;
      asnp->abp = get_astr_textid(asnp, name, acc)  + 2;
      break;
    default: 
      return asn_error("get_atr_seqid", "", -1, asnp,4);
    }
  }
  return asnp->abp;
}

/*
Bioseq ::= SEQUENCE {
    id SET OF Seq-id ,            -- equivalent identifiers
    descr Seq-descr OPTIONAL , -- descriptors
    inst Seq-inst }            -- the sequence data
*/

unsigned char *
get_astr_bioseq(struct asn_bstruct *asnp,
		int *gi,
		char *name,
		char *acc,
		char *descr,
		unsigned char **query,
		int *nq
		) {

  int end_seq = 0;

  asnp->abp = chk_asn_buf(asnp,32);

  if (ABP == ASN_SEQ) {
    end_seq++;
    ABP_INC2;
  }

  if (ABP != ASN_BIOSEQ_ID) {
    fprintf(stderr, "Bioseq - missing ID tag: %2x %2x\n",ABP, asnp->abp[1]);
    return asnp->abp;
  }
  else {
  /* skip over bioseq-id tag */
    ABP_INC2;
    if (ABP == ASN_SETOF) {	/* jump over ASN_SETOF */
      ABP_INC2;
      asnp->abp = get_astr_seqid(asnp, gi, name, acc);
      ABP_INC2; 		/* close ASN_SETOF */
    }
    else {
      return asn_error("get_astr_bioseq","ASN_SEQOF", ASN_SEQOF, asnp, 4);
    }
    ABP_INC2;	/* jump over seq-id tag end */
  }

  if (ABP == ASN_BIOSEQ_DESCR) {
    ABP_INC2;
    asnp->abp = get_astr_seqdescr(asnp, descr);
    ABP_INC2; 		/* skip nulls */
  }
  else { descr[0] = '\0';}

  while (ABP == '\0') { ABP_INC2;}

  if (ABP != ASN_BIOSEQ_INST) {
    fprintf(stderr, "Bioseq - missing ID tag: %2x %2x\n",ABP, asnp->abp[1]);
    return asnp->abp;
  }
  else { 
    ABP_INC2;
    asnp->abp = get_astr_seqinst(asnp, query, nq);
    ABP_INC2; 		/* skip nulls */
  }

  if (end_seq--) {
    ABP_INC2;
  }
  return asnp->abp;
}

unsigned char *
get_pssm_freqs(struct asn_bstruct *asnp,
	       double **freqs,
	       int n_rows,  
	       int n_cols,
	       int by_row) {

  int i_rows, i_cols;
  int in_seq = 0;
  double f_val;

  asnp->abp = chk_asn_buf(asnp,4);

  if (ABP == ASN_SEQ) {
    in_seq = 1;
    ABP_INC2;
    in_seq = 1;
  }

  if (!by_row) {
    for (i_cols = 0; i_cols < n_cols; i_cols++) {
      for (i_rows = 0; i_rows < n_rows; i_rows++) {
	asnp->abp = get_astr_packedfloat(asnp, &f_val);
	freqs[i_cols][i_rows] = f_val;
      }
    }
  }
  else {
    for (i_rows = 0; i_rows < n_rows; i_rows++) {
      for (i_cols = 0; i_cols < n_cols; i_cols++) {
	asnp->abp = get_astr_packedfloat(asnp, &f_val);
	freqs[i_rows][i_cols] = f_val;
      }
    }
  }
  if (in_seq) {asnp->abp +=2;}	/* skip nulls */
  ABP_INC2;
  return asnp->abp;
}

unsigned char *
get_pssm_intermed(struct asn_bstruct *asnp,
		  double **freqs,
		  int n_rows,
		  int n_cols,
		  int by_row) {

  asnp->abp = chk_asn_buf(asnp,4);

  if (ABP == ASN_SEQ) {
    ABP_INC2;
    if (ABP == ASN_PSSM_FREQS) {
      ABP_INC2;
      asnp->abp = get_pssm_freqs(asnp, freqs, n_rows, n_cols, by_row);
    }
    asnp->abp +=2;	/* skip nulls */
  }
  ABP_INC2;
  return asnp->abp;
}


#define ASN_PSSM_PARAMS 161
#define ASN_PSSM_PARAMS_PSEUDOCNT 160
#define ASN_PSSM_PARAMS_RPSPARAMS 161
#define ASN_PSSM_RPSPARAMS_MATRIX 160
#define ASN_PSSM_RPSPARAMS_GAPOPEN 161
#define ASN_PSSM_RPSPARAMS_GAPEXT 162

unsigned char *
get_pssm_rpsparams(struct asn_bstruct *asnp,
	       char *matrix,
	       int *gap_open,
	       int *gap_ext) {

  int end_seq=0;

  asnp->abp = chk_asn_buf(asnp,4);

  if (ABP == ASN_SEQ) {
    ABP_INC2;
    end_seq++;
  }

  if (ABP == ASN_PSSM_RPSPARAMS_MATRIX) {
    ABP_INC2;
    asnp->abp = get_astr_str(asnp, matrix, MAX_SSTR) + 2;
  }

  if (ABP == ASN_PSSM_RPSPARAMS_GAPOPEN) {
    ABP_INC2;
    asnp->abp = get_astr_int(asnp, gap_open)+2;
  }

  if (ABP == ASN_PSSM_RPSPARAMS_GAPEXT) {
    ABP_INC2;
    asnp->abp = get_astr_int(asnp, gap_ext)+2;
  }

  if (end_seq) { chk_asn_buf(asnp,end_seq * 2); }
  while (end_seq-- > 0) { ABP_INC2; }
  return asnp->abp;
}

unsigned char *
get_pssm_params(struct asn_bstruct *asnp,
	       int *pseudo_cnts,
	       char *matrix,
	       int *gap_open,
	       int *gap_ext) {

  int end_seq=0;

  asnp->abp = chk_asn_buf(asnp,6);

  if (ABP == ASN_SEQ) {
    ABP_INC2;
    end_seq++;
  }

  if (ABP == ASN_PSSM_PARAMS_PSEUDOCNT) {
    ABP_INC2;
    asnp->abp = get_astr_int(asnp, pseudo_cnts)+2;
  }

  if (ABP == ASN_PSSM_PARAMS_RPSPARAMS) {
    ABP_INC2;
    asnp->abp = get_pssm_rpsparams(asnp, matrix, gap_open, gap_ext);
    ABP_INC2;
  }
  while (end_seq-- > 0) { ABP_INC2; }
  return asnp->abp;
}

unsigned char *
get_pssm2_scores(struct asn_bstruct *asnp,
		 int *have_scores
		 ) {

  int end_seq=0;

  if (have_scores != NULL) *have_scores = 0;

  if (ABP == ASN_SEQ) {
    end_seq++;
    ABP_INC2;
  }

  if (ABP == '\0') {	/* no scores */
    if (end_seq) ABP_INC2;
  }
  else {
    *have_scores = 1;
  }
  return asnp->abp;
}

unsigned char *
get_pssm2_intermed(struct asn_bstruct *asnp,
		   double ***freqs,
		   int n_rows,
		   int n_cols) {

  int i;
  double **my_freqs;

  if ((my_freqs = (double **) calloc(n_cols, sizeof(double *)))==NULL) {
    fprintf(stderr, " cannot allocate freq cols - %d\n", n_cols);
    exit(1);
  }

  if ((my_freqs[0] = (double *) calloc(n_cols * n_rows, sizeof(double)))==NULL) {
    fprintf(stderr, " cannot allocate freq rows * cols - %d * %d\n", n_rows, n_cols);
    exit(1);
  }

  for (i=1; i < n_cols; i++) {
    my_freqs[i] = my_freqs[i-1] + n_rows;
  }

  *freqs = my_freqs;

  chk_asn_buf(asnp, 8);

  return get_pssm_freqs(asnp, my_freqs, n_rows, n_cols, 0);
}

int
parse_pssm2_asn(struct asn_bstruct *asnp,
		int *gi,
		char *name,
		char *acc,
		char *descr,
		unsigned char **query, 
		int *nq,
		int *n_rows,
		int *n_cols,
		double ***freqs,
		int *pseudo_cnts,
		char *matrix, 
		double *lambda_p) {

  int is_protein;
  int have_rows=0, have_cols=0;
  int have_scores=0;

  chk_asn_buf(asnp, 32);

  /* first get the query */

  if (memcmp(asnp->abp, "\241\2000\200",4) != 0) {
    asn_error("parse_pssm2_asn","ASN_PSSM2_QUERY",ASN_PSSM2_QUERY,asnp,4);
    return -1;
  }
  else {
    asnp->abp+=4;
    asnp->abp = get_astr_bioseq(asnp, gi, name, acc, descr, query, nq) + 4;
  }

  /* finish up the nulls */
  /* perhaps we have parsed correctly and do not need this */
  /*   while (ABP == '\0') { ABP_INC2;} */

  if (memcmp(asnp->abp, "\242\2000\200",4) != 0) {
    asn_error("parse_pssm2_asn","ASN_PSSM2_MATRIX",ASN_PSSM2_MATRIX,asnp,4);
    return -1;
  }
  else {
    asnp->abp+=4;

    if (ABP == ASN_PSSM_IS_PROT) {
      ABP_INC2;
      asnp->abp = get_astr_bool(asnp, &is_protein)+2;
    }

    if (ABP == ASN_PSSM2_MATRIX_NAME) {
      ABP_INC2;
      asnp->abp = get_astr_str(asnp, matrix, MAX_SSTR) + 2;
    }

    if (ABP ==   ASN_PSSM2_NCOLS) {
      ABP_INC2;
      asnp->abp = get_astr_int(asnp, n_cols)+2;
      have_cols = 1;
    }    

    if (ABP ==  ASN_PSSM2_NROWS) {
      ABP_INC2;
      asnp->abp = get_astr_int(asnp, n_rows)+2;
      have_rows = 1;
    }

    if (ABP == ASN_PSSM2_SCORES) {
      /* right now, this is always empty */
      ABP_INC2;
      asnp->abp = get_pssm2_scores(asnp, &have_scores) + 2;
      if (have_scores) return 0;
    }

    if (ABP == ASN_PSSM2_KARLIN_K) {
      ABP_INC2;
      asnp->abp = get_astr_packedfloat(asnp,lambda_p) + 2;
    }

    if (ABP == ASN_PSSM2_FREQS) {
      asnp->abp += 4;
      asnp->abp = get_pssm2_intermed(asnp, freqs, *n_rows, *n_cols) + 4;
    }
  }

  return 1;
}

int
parse_pssm_asn(FILE *afd,
	       int *gi,
	       char *name,
	       char *acc,
	       char *descr,
	       unsigned char **query, 
	       int *nq,
	       int *n_rows,
	       int *n_cols,
	       double ***freqs,
	       int *pseudo_cnts,
	       char *matrix,
	       int *gap_open,
	       int *gap_ext,
	       double *lambda_p) {

  int is_protein, pssm_version;
  int i;
  int have_rows, have_cols, by_col;
  double **my_freqs;
  struct asn_bstruct *asnp;

  asnp = new_asn_bstruct(ASN_BUF);

  asnp->fd = afd;
  asnp->len = ASN_BUF;
  asnp->abp = asnp->buf_max = asnp->buf + ASN_BUF;

  chk_asn_buf(asnp, 32);

  if (memcmp(asnp->abp, "0\200\240\200",4) != 0) {
    fprintf(stderr, "improper PSSM header -");
    return -1;
  }
  else {asnp->abp+=4;}

  if (ABP == ASN_IS_INT) {
    asnp->abp = get_astr_int(asnp, &pssm_version)+2;
    if (pssm_version != 2) {
      fprintf(stderr, "PSSM2 version mismatch: %d\n",pssm_version);
      return -1;
    }
    *gap_open = *gap_ext = 0;
    return parse_pssm2_asn(asnp, gi, name, acc, descr,
			   query, nq,
			   n_rows, n_cols, freqs,
			   pseudo_cnts, matrix,
			   lambda_p);
  }

  if (ABP == ASN_SEQ) { asnp->abp += 2;  }

  if (ABP == ASN_PSSM_IS_PROT ) {
    ABP_INC2;
    asnp->abp = get_astr_bool(asnp, &is_protein)+2;
  }

  if (ABP == ASN_PSSM_NROWS ) {
    ABP_INC2;
    asnp->abp = get_astr_int(asnp, n_rows)+2;

    if (*n_rows > 0) { have_rows = 1; }
    else {
      fprintf(stderr, " bad n_row count\n");
      exit(1);
    }
  }

  if (ABP == ASN_PSSM_NCOLS ) {
    ABP_INC2;
    asnp->abp = get_astr_int(asnp, n_cols)+2;
    if (*n_cols > 0) {
      have_cols = 1;
    }
    else {
      fprintf(stderr, " bad n_row count\n");
      exit(1);
    }
  }

  if (ABP == ASN_PSSM_BYCOL ) {
    ABP_INC2;
    asnp->abp = get_astr_bool(asnp, &by_col)+2;
  }

  /* we have read everything up to the query 

     n_cols gives us the query length, which we can allocate;
  */

  if (ABP == ASN_PSSM_QUERY ) {
    asnp->abp+=4;	/* skip token and CHOICE */
    asnp->abp = get_astr_bioseq(asnp, gi, name, acc, descr, query, nq) + 4;
    *nq = *n_cols;
  }

  /* finish up the nulls */
  while (ABP == '\0') { asnp->abp += 2;}

  if (ABP == ASN_PSSM_INTERMED_DATA) {

    if (!have_rows || !have_cols) {
      fprintf(stderr, " cannot allocate freq - missing rows/cols - %d/%d\n",
	      have_rows, have_cols);
      return -1;
    }

    if ((my_freqs = (double **) calloc(*n_cols, sizeof(double *)))==NULL) {
      fprintf(stderr, " cannot allocate freq cols - %d\n", *n_cols);
      return -1;
    }

    if ((my_freqs[0] = (double *) calloc(*n_cols * *n_rows, sizeof(double)))==NULL) {
      fprintf(stderr, " cannot allocate freq rows * cols - %d * %d\n", *n_rows, *n_cols);
      return -1;
    }
    for (i=1; i < *n_cols; i++) {
      my_freqs[i] = my_freqs[i-1] + *n_rows;
    }

    *freqs = my_freqs;

    ABP_INC2;
    asnp->abp = get_pssm_intermed(asnp, my_freqs, *n_rows, *n_cols, by_col);
    asnp->abp += 4;
  }

  if (ABP == ASN_PSSM_PARAMS ) {
      ABP_INC2;
      asnp->abp = get_pssm_params(asnp, pseudo_cnts, matrix, gap_open, gap_ext) + 2;
  }
  else if (ABP == 0) {ABP_INC2;}

  free_asn_bstruct(asnp);

  return 1;
}

int
parse_pssm_asn_fa( FILE *fd, 
		   int *n_rows_p, int *n_cols_p,
		   unsigned char **query,
		   double ***freq2d,
		   char *matrix,
		   int *gap_open_p,
		   int *gap_extend_p,
		   double *lambda_p) {

  int qi, rj;
  int gi;
  double tmp_freqs[COMPO_LARGEST_ALPHABET];
  char name[MAX_SSTR], acc[MAX_SSTR], descr[MAX_STR];
  int  nq;
  int pseudo_cnts, ret_val;

  /* parse the file */

  ret_val = parse_pssm_asn(fd, &gi, name, acc, descr, query, &nq,
			   n_rows_p, n_cols_p, freq2d,
			   &pseudo_cnts, matrix, gap_open_p, gap_extend_p,
			   lambda_p);

  if (ret_val <=0) return ret_val;

  for (qi = 0; qi < *n_cols_p; qi++) {
    for (rj = 0; rj < *n_rows_p; rj++) { tmp_freqs[rj] = (*freq2d)[qi][rj];}

    for (rj = 0; rj < COMPO_NUM_TRUE_AA; rj++) {
      (*freq2d)[qi][rj] = tmp_freqs[pssm_aa_order[rj]];
    }
  }

  return 1;
}
