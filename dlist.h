/*
 * Copyright 2012 Johan Eliasson (johane@cs.umu.se). Tillåtelse ges för användning på kurserna 
 * i Datastrukturer och algoritmer vid Umeå Universitet. All annan användning kräver författarens
 * tillstånd.
 */

/*
Implementation av datatypen riktad lista.

Som standard så är användaren av datatypen ansvarig för att avallokera
minnet för datavärdena. Genom att anropa dlist_setMemHandler och till denna
skicka med en funktion för att avallokera ett dataobjekt så kommer listan
ta över ansvaret för minneshanteringen och automatiskt avallokera värdena
då de tas bort från listan. OBS i detta fall så måste användaren kopiera
värdena om de ska finnas kvar efter att ha tagits bort från listan.
*/

#ifndef _DLIST_H
#define _DLIST_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef __DATA
#define __DATA
typedef void *data;
#endif

#ifndef __MEMFREEDATAFUNC
#define __MEMFREEDATAFUNC
typedef void memFreeFunc(data);
#endif

typedef struct link {
	data data;
	struct link *next;
} element;

typedef element *dlist_position;

struct list {
	element *head;
        memFreeFunc *freeFunc;
};
typedef struct list dlist;

/*
Syfte: Skapa en ny tom lista.
Returvärde: Pekare till den nyskapade listan.
Kommentarer: Då man använt listan färdigt så måste minnet för listan
             avallokeras via funktionen dlist_free
*/
dlist *dlist_empty(void);

/*
Syfte: Installera en minneshanterare för listan så att den kan ta över
       ansvaret för att avallokera minnet för värdena då de ej finns kvar
       i listan mer.
Parametrar: l - listan
            f - en funktionspekare till en funktion som tar in ett värde
                av typen data (void pekare) och avallokerar minnet för värdet
                den pekar på (ett av den typ av värden som lagras i listan).
Kommentarer: Bara minne för värden som finns i listan vid tidpunkten då en
             minneshanterare satts eller senare kommer tas bort.
             Listan funkar även utan att denna funktion anropas,
             men det är då upp till användaren av datatypen att avallokera allt
             minne för datat som lagras i listan.
*/
void dlist_setMemHandler(dlist *l, memFreeFunc *f);

/*
Syfte: Returnerar listans första position
Parametrar: l - listan
Returvärde: den första positionen i listan
Kommentarer:
*/
dlist_position dlist_first(dlist *l);

/*
Syfte: Hämta efterföljaren till en given position
Parametrar: l - listan
            p - positionen man vill veta efterföljaren till
Returvärde: positionen för efterföljaren
Kommentarer: Odefinierad för end positionen
*/
dlist_position dlist_next(dlist *l,dlist_position p);

/*
Syfte: Kontrollera om listan är tom.
Parametrar: l - listan
Returvärde: true om listan är tom, false annars
Kommentarer:
*/
bool dlist_isEmpty(dlist *l);

/*
Syfte: Flytta en länk längst fram i listan
Parametrar: l - listan
            p - positionen
Returvärde: Första positionen i listan
*/
dlist_position dlist_moveToFront(dlist *l,dlist_position p);

/*
Syfte: Sätta in ett värde i listan på en given position
Parametrar: l - listan
            p - positionen
            d värdet som ska sättas in
Returvärde: Positionen för det nyinsatta värdet.
Kommentarer: p bör ej användas efter anropet
*/

dlist_position dlist_insert(dlist *l,dlist_position p,data d);

/*
Syfte: Ta bort ett värde på en given position i listan
Parametrar: l - listan
            p - positionen för värdet som ska tas bort
Returvärde: positionen där värdet togs bort
Kommentarer: Returvärdet bör användas istället för p efter ett anrop
             till funktionen
*/
dlist_position dlist_remove(dlist *l, dlist_position p);

/*
Syfte: Avallokerar allt inne som används av listan. Och om en funktion
       för att avallokera minnet för datavärdena i listan installerats
       mha dlist_setMemHandler kommer även minnet för datat i listan avallokeras.
Parametrar: l - listan
Kommentarer: Efter ett anrop till denna funktion så är alla funktioner på listan
             odefinierade.
*/
void dlist_free(dlist *l);

/*
Syfte: Hämta ett värde på en given position från listan
Parametrar: l - listan
            p - positionen i listan
Returvärde: värdet som fanns på positionen p
Kommentarer:
*/
data dlist_inspect(dlist *l, dlist_position p);

/*
Syfte: Kolla om en given position är positionen efter det sista värdet i listan
Parametrar: l - listan
            p - positionen man vill kontrollera om den är positionen efter det sista värdet.
Returvärde: true om p är positionen efter sista värdet, false annars.
Kommentarer:
*/
bool dlist_isEnd(dlist *l, dlist_position p);

#endif
