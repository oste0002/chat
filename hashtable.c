#include "hashtable.h"

//Written by Johan Eliasson <johane@cs.umu.se>.
//May be used in the course Datastrukturer och Algoritmer (C) at Umeå University.
//Usage exept those listed above requires permission by the author.

#define EMPTY_MARKER ((void *) 1)

/* intern hjälpfunktion ej för externt bruk */
static unsigned internal_hash(hashtable_t *t,ht_key_t key) {
	return t->hash_fnk(key)%t->size;
}

/* intern hjälpfunktion ej för externt bruk */
static void rehash(hashtable_t *t) {
	int i;
	key_valuestore_t **temp=t->data;
	int oldsize=t->size;
	t->data=calloc(oldsize*2,sizeof(key_valuestore_t *));
	t->size=oldsize*2;
	t->numelem=0;
	t->numinsertions=0;
	for(i=0;i<oldsize;i++) {
		if(temp[i]) {
			hashtable_insert(t,temp[i]->key,temp[i]->value);
			free(temp[i]);
		}
	}
	free(temp);
}

//Function for creating a new empty hashtable
//Paramerers:
//   size:Starting size of table should be approx 2*estimated amount of
//        values to insert (the size will grow if needed but that operation
//        is expensive)
//   hash_fkn:A function for computing a hash from a key. The return value
//            of the function should be in the range 0-UINT_MAX. The value
//            will internaly be converted to a value in the range 0-size
//   cmp_fnk:A function for comparing two keys. Should return 0 for keys
//           that are not equal and a value <> 0 for equal keys
//Return value:
//   The function will return an empty hashtable.
hashtable_t *hashtable_empty(unsigned size,hash_fnk_t hash_fnk,cmp_fnk_t cmp_fnk) {
	hashtable_t *t=malloc(sizeof(hashtable_t));
	t->cmp_fnk=cmp_fnk;
	t->hash_fnk=hash_fnk;
	t->memfreeKey=NULL;
	t->memfreeValue=NULL;
	t->data=calloc(size,sizeof(key_valuestore_t *));
	t->size=size;
	t->numelem=0;
	t->numinsertions=0;
	return t;
}

/*
	 Function for installing a memory handler so that it can manage dynamic memory used for keys.
	 When a memory handler is installed the hashtable will free all keys that it no longer needs using this function,
Parameters: t - The hashtable
f - A funktion that should free memory for one key
Used by the hashtable to manage memory.
Comments:
Should be called directly after creation of the hashtable. The hastable will work
even if this function is not used, but the responsibility to free the memory
for the keys then falls on the users of the table.
*/
void hashtable_setKeyMemHandler(hashtable_t *t, memfreeKey_t f) {
	t->memfreeKey=f;
}

/*
	 Function for installing a memory handler so that it can manage dynamic memory used for values.
	 When a memory handler is installed the hashtable will free all values that it no longer needs using this function,
Parameters: t - The hashtable
f - A funktion that should free memory for one value
Used by the hashtable to manage memory.
Comments:
Should be called directly after creation of the hashtable. The hastable will work
even if this function is not used, but the responsibility to free the memory
for the values then falls on the users of the table.
*/
void hashtable_setValueMemHandler(hashtable_t *t, memfreeValue_t f) {
	t->memfreeValue=f;
}

//Function for looking up a value in a hashtable.
//Parameters:
//    t:The hashtable
//    key:The key
//Return value:
//    Will return NULL if the key is not found or else a pointer to a value
//    corresponding to the key. If a memHandler is set that free the value then
//    the hashtable manages the memory for the
//    return value. In that case if the value is to be used after removal or insertions using
//    the same key, or removal of the table, the caller should copy the value.
ht_value_t hashtable_lookup(hashtable_t *t, ht_key_t key) {
	int index=internal_hash(t,key);
	while (t->data[index]==EMPTY_MARKER||
			(t->data[index]&&!t->cmp_fnk(t->data[index]->key,key)))
		index=(index+1)%t->size;
	return t->data[index]?t->data[index]->value:NULL;
}

//Function for inserting a value into the hashtable. If memhandlers are set the hashtable will
//take ownership of and manage the memory used by the value and key using
//the memory handling functions.
void hashtable_insert(hashtable_t *t, ht_key_t key, ht_value_t value) {
	if(t->numinsertions>=t->size/2)
		rehash(t);
	int index=internal_hash(t,key);
	while (!(t->data[index]==EMPTY_MARKER)
			&&t->data[index] && !t->cmp_fnk(t->data[index]->key,key))
		index=(index+1)%t->size;
	if(!t->data[index]||t->data[index]==EMPTY_MARKER) {
		t->data[index]=malloc(sizeof(key_valuestore_t));
		t->numelem++;
		t->numinsertions++;
	}
	else {
		if(t->memfreeKey!=NULL) 
			t->memfreeKey(t->data[index]->key);
		if(t->memfreeValue!=NULL)
			t->memfreeValue(t->data[index]->value);
	}
	t->data[index]->key=key;
	t->data[index]->value=value;
}

//Function for checking i the hashtable is empty.
bool hashtable_isEmpty(hashtable_t *t) {
	return t->numelem==0;
}

//Function for removing the mapping between a key and a value in the hashtable
//Memory used by the inserted key and value will be freed.
void hashtable_remove(hashtable_t *t, ht_key_t key) {
	int index=internal_hash(t,key);
	while (t->data[index]==EMPTY_MARKER||
			(t->data[index] && !t->cmp_fnk(t->data[index]->key,key)))
		index=(index+1)%t->size;
	if(t->data[index]) {
		if(t->memfreeKey!=NULL) 
			t->memfreeKey(t->data[index]->key);
		if(t->memfreeValue!=NULL)
			t->memfreeValue(t->data[index]->value);
		free(t->data[index]);
		t->data[index]=EMPTY_MARKER;
		t->numelem--;
	}
}

//Function for releasing all the memory used by the hashtable. The
//hashtable is invalid after a call to this function.
void hashtable_free(hashtable_t *t) {
	unsigned int i;
	for(i=0;i<t->size;i++) {
		if(t->data[i]!=EMPTY_MARKER&&t->data[i]!=NULL) {
			if(t->memfreeKey!=NULL)
				t->memfreeKey(t->data[i]->key);
			if(t->memfreeValue!=NULL)
				t->memfreeValue(t->data[i]->value);
			free(t->data[i]);
		}
	}
	free(t->data);
	free(t);
}

/* Hash function that calculates a unique unsigned out of a string
 *
 * Arguments:
 *   str2 - string to be hashed
 *
 * Return: unsigned - hash value
 */
unsigned strhash(void *str2) {
	char *str=str2;
	unsigned hash=0;
	int i=0;
	while(str[i]) {
		hash ^= str[i]<<(i*4%(sizeof(int)*8));
		i++;
	}
	return hash;
}

bool strcmp2(void *str1,void *str2) {
  return strcmp((char *)str1,(char *)str2)==0;
}















