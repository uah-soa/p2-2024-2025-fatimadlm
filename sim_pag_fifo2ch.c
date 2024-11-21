/*
    Copyright 2023 The Operating System Group at the UAH
    sim_pag_fifo2ch.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "./sim_paging.h"

// Function that initialises the tables
void init_tables(ssystem* S) {
    int i;

    // Reset pages
    memset(S->pgt, 0, sizeof(spage) * S->numpags);

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

    page = virtual_addr / S->pagsz;  // Page number
    offset = virtual_addr % S->pagsz; // Offset within the page

    if (page < 0 || page >= S->numpags) {
        S->numillegalrefs++;  // Out-of-range references
        return ~0U;           // Return invalid physical address
    }

    if (!S->pgt[page].present) {
        handle_page_fault(S, virtual_addr);  // Trigger page fault if not present
    }

    frame = S->pgt[page].frame;
    physical_addr = frame * S->pagsz + offset;

    reference_page(S, page, op);  // Mark page as referenced

    if (S->detailed) {
        printf("\t %c %u==P %d(M %d)+ %d\n", op, virtual_addr, page, frame, offset);
    }

    return physical_addr;
}

void reference_page(ssystem* S, int page, char op) {
    if (op == 'R') {
        S->numrefsread++;  // Contamos las lecturas de la página
    } else if (op == 'W') {
        S->pgt[page].modified = 1;  // Marcamos la página como modificada
        S->numrefswrite++;          // Contamos las escrituras de la página
    }
    S->pgt[page].referenced = 1;  // Marcamos la página como referenciada
}

// Functions that simulate the operating system
void handle_page_fault(ssystem* S, unsigned virtual_addr) {
    int page, victim, frame, last;

    S->numpagefaults++;  // Incrementamos el número de fallos de página
    page = virtual_addr / S->pagsz;

    if (S->detailed) {
        printf("@ PAGE_FAULT in P %d!\n", page);  // Mostramos el fallo de página
    }

    // Si hay marcos libres, ocupamos uno con la nueva página
    if (S->listfree != -1) {
        last = S->listfree;
        frame = S->frt[last].next;
        if (frame == last) {
            S->listfree = -1;  // La lista de marcos libres queda vacía
        } else {
            S->frt[last].next = S->frt[frame].next;  // Avanzamos al siguiente marco libre
        }
        occupy_free_frame(S, frame, page);  // Ocupamos el marco libre con la nueva página
    } else {
        // Si no hay marcos libres, elegimos uno para reemplazar
        victim = choose_page_to_be_replaced(S);
        replace_page(S, victim, page);  // Reemplazamos la página víctima con la nueva página
    }
}
int choose_page_to_be_replaced(ssystem* S) {
    int frame, victim;

    while (1) {
        frame = S->frt[S->listoccupied].next;
        int page = S->frt[frame].page;

        if (S->pgt[page].referenced) {
            // Si la página ha sido referenciada, le damos una segunda oportunidad
            S->pgt[page].referenced = 0;  // Reiniciamos el bit de referencia
            S->listoccupied = frame;     // Avanzamos al siguiente marco
        } else {
            // Si no ha sido referenciada, la elegimos como víctima
            victim = page;
            break;
        }
    }

    if (S->detailed) {
        printf("@ Choosing (at FIFO 2nd chance) P%d of F%d to be replaced\n", victim, frame);  // Mostramos qué página fue elegida para reemplazar
    }

    return victim;  // Devolvemos la página víctima
}


void replace_page(ssystem* S, int victim, int newpage) {
    int frame = S->pgt[victim].frame;

    if (S->pgt[victim].modified) {
        if (S->detailed) {
            printf("@ Writing modified P%d back (to disc) to replace it\n", victim);  // Mostramos si la página víctima fue modificada y necesita ser escrita en el disco
        }
        S->numpgwriteback++;  // Incrementamos el contador de escrituras de páginas modificadas
    }

    if (S->detailed) {
        printf("@ Replacing victim P%d with P%d in F%d\n", victim, newpage, frame);  // Mostramos el reemplazo de páginas
    }

    // Actualizamos la tabla de páginas
    S->pgt[victim].present = 0;
    S->pgt[newpage].present = 1;
    S->pgt[newpage].frame = frame;
    S->pgt[newpage].modified = 0;

    S->frt[frame].page = newpage;  // Actualizamos el marco con la nueva página
}

void occupy_free_frame(ssystem* S, int frame, int page) {
    if (S->detailed) {
        printf("@ Storing P%d in F%d\n", page, frame);  // Mostramos qué página se ha almacenado en qué marco
    }

    // Si la lista de marcos ocupados está vacía, inicializamos la lista
    if (S->listoccupied == -1) {
        S->frt[frame].next = frame;  // Si solo hay un marco, lo apuntamos a sí mismo
    } else {
        S->frt[frame].next = S->frt[S->listoccupied].next;  // Insertamos el nuevo marco en la lista de ocupados
        S->frt[S->listoccupied].next = frame;  // Actualizamos la lista ocupada
    }
    S->listoccupied = frame;  // El marco recién ocupado es el primero en la lista

    // Actualizamos la tabla de páginas
    S->pgt[page].present = 1;
    S->pgt[page].frame = frame;
    S->pgt[page].modified = 0;
    S->pgt[page].referenced = 1;  // Marcamos la página como referenciada
    S->frt[frame].page = page;  // Actualizamos el marco con la nueva página
}
// Functions that show results
void print_page_table(ssystem* S) {
    printf("%10s %10s %10s %10s %10s\n", "PAGE", "Present", "Frame", "Modified", "Referenced");

    for (int p = 0; p < S->numpags; p++) {
        if (S->pgt[p].present) {
            printf("%8d   %6d     %8d   %6d     %6d\n",
                   p, S->pgt[p].present, S->pgt[p].frame,
                   S->pgt[p].modified, S->pgt[p].referenced);
        } else {
            printf("%8d   %6d     %8s   %6s     %6s\n", p, S->pgt[p].present, "-", "-", "-");
        }
    }
}

void print_frames_table(ssystem* S) {
    printf("%10s %10s %10s %10s\n", "FRAME", "Page", "Modified", "Referenced");

    for (int f = 0; f < S->numframes; f++) {
        int page = S->frt[f].page;

        if (page == -1) {
            printf("%8d   %8s     %6s       %6s\n", f, "-", "-", "-");
        } else {
            printf("%8d   %8d     %6d       %6d\n", f, page, S->pgt[page].modified, S->pgt[page].referenced);
        }
    }
}

void print_replacement_report(ssystem* S) {
    printf("FIFO second chance\n Frames:\n");
    for (int i = 0; i < S->numframes; i++) {
        printf("Frame: %d   Page: %d  Reference bit: %d\n", i, S->frt[i].page, S->pgt[S->frt[i].page].referenced);
    }
}
