/*
Heavily taken from 
http://www.thelearningpoint.net/computer-science/data-structures-singly-linked-list-with-c-program-source-code
*/
#include <stdio.h>
#include <stdlib.h>

typedef struct Node {
        int data;
        struct Node *next;
} node;

void insert(node *pointer, int data) {
        while(pointer->next!=NULL) {
            pointer = pointer -> next;
        }
        pointer->next = (node *)malloc(sizeof(node));
        pointer = pointer->next;
        pointer->data = data;
        pointer->next = NULL;
}

int find(node *pointer, int key) {
        pointer = pointer -> next;
        while(pointer!=NULL) {
                if(pointer->data == key) {
                    return 1;
                }
                pointer = pointer -> next;
        }
        return 0;
}

void delete(node *pointer, int data) {
        while (pointer->next!=NULL && (pointer->next)->data != data) {
                pointer = pointer -> next;
        }
        if (pointer->next==NULL) {
                printf("Element %d is not present in the list\n",data);
                return;
        }
        node *temp;
        temp = pointer -> next;
        pointer->next = temp->next;
        free(temp);
        return;
}

void print(node *pointer) {
        if(pointer==NULL) {
            return;
        }
        printf("%d ",pointer->data);
        print(pointer->next);
}

int main() {
        /* initialization */
        node *start,*temp;
        start = (node *)malloc(sizeof(node)); 
        temp = start;
        temp -> next = NULL;

        /* test harness */
        insert(start, 2);
        delete(start, 2);
        int status = find(start, 2);
        if(status) {
            printf("Element Found\n");
        } else {
            printf("Element Not Found\n");
        }
        insert(start, 5);
	insert(start, 10);
	// interrupt malloc
	void *ptr1 = malloc(5 * sizeof(int));
	insert(start, 22);
	void *ptr2 = malloc(5 * sizeof(int));
	insert(start, 7);
	insert(start, 9);
	insert(start, 2);
	void *ptr3 = malloc(5 * sizeof(int));
	insert(start, 11);
	insert(start, 11);
	insert(start, 77);
	void *ptr4 = malloc(5 * sizeof(int));
	insert(start, 62);
	insert(start, 29);
	// cleaning up
	free(ptr1);
	free(ptr2);
	free(ptr3);
	free(ptr4);
        status = find(start, 5);
        if(status) {
            printf("Element Found\n");
        } else {
            printf("Element Not Found\n");
        }
        printf("The list is ");
        print(start->next);
        printf("\n");
        delete(start, 5);
}
