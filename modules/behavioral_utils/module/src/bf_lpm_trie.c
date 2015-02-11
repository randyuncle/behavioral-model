#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <Judy.h>

typedef unsigned char byte_t;

typedef struct node_s {
  Pvoid_t PJLarray_branches;
  Pvoid_t PJLarray_prefixes;
  unsigned char branch_num;
  unsigned char pref_num;
  struct node_s *parent;
  byte_t child_id;
} node_t;

struct bf_lpm_trie_s {
  node_t *root;
  size_t key_width_bytes;
  bool release_memory;
};

typedef struct bf_lpm_trie_s bf_lpm_trie_t;

static inline void allocate_node(node_t ** node) {
  *node = (node_t *) malloc(sizeof(node_t));
  memset(*node, 0, sizeof(node_t));
}

bf_lpm_trie_t *bf_lpm_trie_create(size_t key_width_bytes, bool auto_shrink) {
  assert(key_width_bytes <= 64);
  bf_lpm_trie_t *trie = (bf_lpm_trie_t *) malloc(sizeof(bf_lpm_trie_t));
  trie->key_width_bytes = key_width_bytes;
  trie->release_memory = auto_shrink;
  allocate_node(&trie->root);
  return trie;
}

static void destroy_node(node_t *node) {
  Word_t index = 0;
  Word_t *pnode;
  Word_t rc_word;
  JLF(pnode, node->PJLarray_branches, index);
  while (pnode != NULL) {
    destroy_node((node_t *) *pnode);
    JLN(pnode, node->PJLarray_branches, index);    
  }
  JLFA(rc_word, node->PJLarray_branches);
  JLFA(rc_word, node->PJLarray_prefixes);
  free(node);
}

void bf_lpm_trie_destroy(bf_lpm_trie_t *t) {
  destroy_node(t->root);
  free(t);
}

static inline node_t *get_next_node(node_t *current_node, byte_t byte) {
  Word_t *pnode;
  JLG(pnode, current_node->PJLarray_branches, (Word_t) byte);
  if(!pnode) return NULL;
  return (node_t *) *pnode;
}

static inline void set_next_node(node_t *current_node, byte_t byte,
				 node_t *next_node) {
  Word_t *pnode;
  JLI(pnode, current_node->PJLarray_branches, (Word_t) byte);
  *pnode = (Word_t) next_node;
}

static inline int delete_branch(node_t *current_node, byte_t byte) {
  int rc;
  JLD(rc, current_node->PJLarray_branches, (Word_t) byte);
  return rc;
}

static inline unsigned short get_prefix_key(unsigned prefix_length,
					    byte_t byte) {
  return prefix_length? (byte >> (8 - prefix_length)) + (prefix_length << 8) :0;
}

/* returns 1 if was present, 0 otherwise */
static inline int insert_prefix(node_t *current_node,
				unsigned short prefix_key,
				void *value) {
  Word_t *pvalue;
  int rc;
  JLI(pvalue, current_node->PJLarray_prefixes, (Word_t) prefix_key);
  rc = (*pvalue) ? 1 : 0;
  *pvalue = (Word_t) value;
  return rc;
}

static inline void *get_prefix(node_t *current_node,
			       unsigned short prefix_key) {
  Word_t *pvalue;
  JLG(pvalue, current_node->PJLarray_prefixes, (Word_t) prefix_key);
  if(!pvalue) return NULL;
  return (void *) *pvalue;
}

static inline void **get_prefix_ptr(node_t *current_node,
				    unsigned short prefix_key) {
  Word_t *pvalue;
  JLG(pvalue, current_node->PJLarray_prefixes, (Word_t) prefix_key);
  return (void **) pvalue;
}

/* returns 1 if was present, 0 otherwise */
static inline int delete_prefix(node_t *current_node,
				unsigned short prefix_key) {
  int rc;
  JLD(rc, current_node->PJLarray_prefixes, (Word_t) prefix_key);
  return rc;
}

void bf_lpm_trie_insert(bf_lpm_trie_t *trie,
			char *prefix, int prefix_length,
			void *value) {
  node_t *current_node = trie->root;
  byte_t byte;
  unsigned short prefix_key;

  while(prefix_length >= 8) {
    byte = (byte_t) *prefix;
    node_t *node = get_next_node(current_node, byte);
    if(!node) {
      allocate_node(&node);
      node->parent = current_node;
      node->child_id = byte;
      set_next_node(current_node, byte, node);
      current_node->branch_num++;
    }

    prefix++;
    prefix_length -= 8;
    current_node = node;
  }

  if(prefix_length == 0)
    prefix_key = 0;
  else 
    prefix_key = get_prefix_key((unsigned) prefix_length, (byte_t) *prefix);

  if(!insert_prefix(current_node, prefix_key, value))
    current_node->pref_num++;
}

void *bf_lpm_trie_get(bf_lpm_trie_t *trie, char *key) {
  node_t *current_node = trie->root;
  byte_t byte;
  size_t key_width = trie->key_width_bytes;
  void *best_fit_data = NULL;
  void *data;
  unsigned short prefix_key;
  unsigned i;

  while(current_node) {
    data = get_prefix(current_node, 0);
    if(data) {
      best_fit_data = data;
    }
    if(key_width > 0) {
      byte = (byte_t) *key;
      for(i = 7; i > 0; i--) {
	prefix_key = get_prefix_key((unsigned) i, byte);
	data = get_prefix(current_node, prefix_key);
	if(data) {
	  best_fit_data = data;
	  break;
	}
      }

      current_node = get_next_node(current_node, byte);
      key++;
      key_width--;
    }
    else {
      break;
    }
  }

  return best_fit_data;
}

void *bf_lpm_trie_delete(bf_lpm_trie_t *trie, char *prefix, int prefix_length) {
  node_t *current_node = trie->root;
  byte_t byte;
  unsigned short prefix_key;
  void **pdata;
  void *data;

  while(prefix_length >= 8) {
    byte = (byte_t) *prefix;
    node_t *node = get_next_node(current_node, byte);
    if(!node) return NULL;

    prefix++;
    prefix_length -= 8;
    current_node = node;
  }

  if(prefix_length == 0)
    prefix_key = 0;
  else 
    prefix_key = get_prefix_key((unsigned) prefix_length, (byte_t) *prefix);

  pdata = get_prefix_ptr(current_node, prefix_key);
  if(!pdata) return NULL;

  data = *pdata;
  *pdata = NULL;

  if(trie->release_memory) {
    assert(delete_prefix(current_node, prefix_key) == 1);
    current_node->pref_num--;
    while(current_node->pref_num == 0 && current_node->branch_num == 0) {
      node_t *tmp = current_node;
      current_node = current_node->parent;
      if(!current_node) break;
      assert(delete_branch(current_node, tmp->child_id) == 1);
      free(tmp);
      current_node->branch_num--;
    }
  }
  
  return data;
}


