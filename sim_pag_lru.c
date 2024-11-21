/*
    Copyright 2023 The Operating System Group at the UAH
    sim_pag_lru.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "./sim_paging.h"

// Function that initialises the tables

void init_tables(ssystem* S) {
  int i;

  // Reset pages
  memset(S->pgt, 0, sizeof(spage) * S->numpags);

  // Empty LRU stack
  S->lru = -1;

  // Reset LRU(t) time
  S->clock = 0;

  // Circular list of free frames
  for (i = 0; i < S->numframes - 1; i++) {
    S->frt[i].page = -1;
    S->frt[i].next = i + 1;
  }

  S->frt[i].page = -1;  // Now i == numframes-1
  S->frt[i].next = 0;   // Close circular list
  S->listfree = i;      // Point to the last one

  // Empty circular list of occupied frames
  S->listoccupied = -1;
}

// Functions that simulate the hardware of the MMU

unsigned sim_mmu(ssystem* S, unsigned virtual_addr, char op) {
  unsigned physical_addr;
  int page, frame, offset;

  page   = virtual_addr / S-> pagsz ;	// Quotient 
  offset = virtual_addr % S-> pagsz ;	// Reminder

  if ( page <0 || page >= S->numpags )
  {
	  S->numillegalrefs++;  // References out of range 
	  return ~0U;	// Return invalid physical 0xFFF..F
  }

  if (! S->pgt[page].present )
	// Not present: trigger page fault exception 
	  handle_page_fault(S, virtual_addr);

  // Now it is present
  frame = S->pgt[page].frame ;	
  physical_addr = frame*S->pagsz+offset;

  reference_page (S, page, op);

  if (S->detailed) {
	  printf ("\t %c %u==P %d(M %d)+ %d\n", op, virtual_addr, page, frame, offset);
  }
  
  return physical_addr;
}

void reference_page(ssystem* S, int page, char op) {
  if (op == 'R') {              // If it's a read,
    S->numrefsread++;           // count it
  } else if (op == 'W') {       // If it's a write,
    S->pgt[page].modified = 1;  // count it and mark the
    S->numrefswrite++;          // page 'modified'
    }
    S->pgt[page].timestamp = S->clock;//Actualizamos la marca de tiempo de la página
    S->clock++;//Incrementamos
 //Control de overflow
if(S->clock == 0)
{
  printf("Cuidadin que hay overflow del reloj :)");
}
}

// Functions that simulate the operating system

void handle_page_fault(ssystem* S, unsigned virtual_addr) {
  int page, victim, frame, last;

  S->numpagefaults ++;
  page = virtual_addr / S-> pagsz;
  if (S->detailed)
	  printf ("@ PAGE_FAULT in P %d!\n", page);

 if (S->listfree != -1) {
	// There are free frames
	last = S->listfree;
	frame = S->frt[last].next;
	  if (frame==last) {
		// Then, this is the last one left.
		  S->listfree = -1;
	  } else {
		// Otherwise, bypass
		  S->frt[last].next = S->frt[frame].next;			
	  }
	occupy_free_frame(S, frame, page);
}
else {
	// There are not free frames
	victim = choose_page_to_be_replaced(S);
	replace_page(S, victim, page);
}
}
static unsigned myrandom(unsigned from,  // <<--- random
                         unsigned size) {
  unsigned n;

  n = from + (unsigned)(rand() / (RAND_MAX + 1.0) * size);

  if (n > from + size - 1)  // These checks shouldn't
    n = from + size - 1;    // be necessary, but it's
  else if (n < from)        // better to not rely too
    n = from;               // much on the floating
                            // point operations
  return n;
}

int choose_page_to_be_replaced(ssystem* S) {
  int frame = -1, victim;//Inicialización

//buscamos la página con la marca de tiempo más baja
  for(int i = 0; i < S->numframes; i++){
    if(frame == -1)
      frame = i;
    if(S->pgt[S->frt[i].page].timestamp < S->pgt[S->frt[frame].page].timestamp)
      frame = i;
  }
  //La almacenamos

  victim = S->frt[frame].page;
//modo detalle
  if (S->detailed)
    printf(
        "@ Choosing (at LRU) P%d of F%d to be "
        "replaced\n",
        victim, frame);

  return victim;
}

void replace_page(ssystem* S, int victim, int newpage) {
  int frame;

  frame = S->pgt[victim].frame;

  if (S->pgt[victim].modified) {
    if (S->detailed)
      printf(
          "@ Writing modified P%d back (to disc) to "
          "replace it\n",
          victim);

    S->numpgwriteback++;
  }

  if (S->detailed)
    printf("@ Replacing victim P%d with P%d in F%d\n", victim, newpage, frame);

  S->pgt[victim].present = 0;

  S->pgt[newpage].present = 1;
  S->pgt[newpage].frame = frame;
  S->pgt[newpage].modified = 0;

  S->frt[frame].page = newpage;
}

void occupy_free_frame(ssystem* S, int frame, int page) {
  if (S->detailed) printf("@ Storing P%d in F%d\n", page, frame);

  S->pgt[page].frame = frame;
  S->frt[frame].page = page;

  S->pgt[page].present = 1;
  S->pgt[page].referenced = 1;
}

// Functions that show results

void print_page_table(ssystem* S) {
  int p;

  printf("%10s %10s %10s %10s   %s\n", "PAGE", "Present", "Frame", "Modified", "Timestamp");

  for (p = 0; p < S->numpags; p++)
    if (S->pgt[p].present)
      printf("%8d   %6d     %8d   %6d   %8d\n", p, S->pgt[p].present, S->pgt[p].frame,
             S->pgt[p].modified, S->pgt[p].timestamp);
    else
      printf("%8d   %6d     %8s   %6s   %8d\n", p, S->pgt[p].present, "-", "-", S->pgt[p].timestamp);
}

void print_frames_table(ssystem* S) {
  int p, f;

  printf("%10s %10s %10s   %s\n", "FRAME", "Page", "Present", "Modified");

  for (f = 0; f < S->numframes; f++) {
    p = S->frt[f].page;

    if (p == -1)
      printf("%8d   %8s   %6s     %6s\n", f, "-", "-", "-");
    else if (S->pgt[p].present)
      printf("%8d   %8d   %6d     %6d\n", f, p, S->pgt[p].present,
             S->pgt[p].modified);
    else
      printf("%8d   %8d   %6d     %6s   ERROR!\n", f, p, S->pgt[p].present,
             "-");
  }
}

void print_replacement_report(ssystem* S) {
  //Inicializamos las variables
  int mintimestamp, maxtimestamp;
  for(int i = 0; i < S->numpags; i++){
    if(i == 0){
      mintimestamp = S->pgt[i].timestamp;
      maxtimestamp = S->pgt[i].timestamp;
    }
    if(S->pgt[i].timestamp < mintimestamp)
      mintimestamp = S->pgt[i].timestamp;
    if(S->pgt[i].timestamp > maxtimestamp)
      maxtimestamp = S->pgt[i].timestamp;
  }


  printf(
      "LRU replacement "
      "(Clock value: %10d, Min timestamp: %10d, Max timestamp: %10d)\n", S->clock, mintimestamp, maxtimestamp);  
}