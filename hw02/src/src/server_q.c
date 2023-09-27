/*******************************************************************************
* Simple FIFO Order Server Implementation
*
* Description:
*     A server implementation designed to process client requests in First In,
*     First Out (FIFO) order. The server binds to the specified port number
*     provided as a parameter upon launch.
*
* Usage:
*     <build directory>/server <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 10, 2023
*
* Notes:
*     Ensure to have proper permissions and available port before running the
*     server. The server relies on a FIFO mechanism to handle requests, thus
*     guaranteeing the order of processing. For debugging or more details, refer
*     to the accompanying documentation and logs.
*
*******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>

/* Needed for wait(...) */
#include <sys/types.h>
#include <sys/wait.h>

/* Needed for semaphores */
#include <semaphore.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"

#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s <port_number>\n"

/* 4KB of stack for the worker thread */
#define STACK_SIZE (4096)

/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */

/* Max number of requests that can be queued */
#define QUEUE_SIZE 500

struct queue {
    struct request requests[QUEUE_SIZE];
    int front;
    int rear;
    int size;
};

void initialize_queue(struct queue *the_queue) {
    the_queue->front = 0;
    the_queue->rear = -1; 
    the_queue->size = 0;
}
struct worker_params {
	struct queue* the_queue;
  	int conn_socket;
};

/* Add a new request <request> to the shared queue <the_queue> */
int add_to_queue(struct request to_add, struct queue * the_queue)
{
	
	int retval = 0;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	if (the_queue->size >= QUEUE_SIZE) {
        perror("Error: The queue is full!");
        retval = -1;
    } else {
        the_queue->rear = (the_queue->rear + 1) % QUEUE_SIZE;
        the_queue->requests[the_queue->rear] = to_add;
        the_queue->size++;
    }
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	sem_post(queue_notify);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Add a new request <request> to the shared queue <the_queue> */
struct request get_from_queue(struct queue * the_queue)
{
	struct request retval;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_notify);
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	if (the_queue->size == 0) {
        perror("Error: The queue is empty!");
    } else {
        retval = the_queue->requests[the_queue->front];
        the_queue->front = (the_queue->front + 1) % QUEUE_SIZE;
        the_queue->size--;
    }
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Implement this method to correctly dump the status of the queue
 * following the format Q:[R<request ID>,R<request ID>,...] */
void dump_queue_status(struct queue * the_queue)
{
	int i;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	printf("Q:[");
    for (i = 0; i < the_queue->size; i++) {
        printf("R%d", the_queue->requests[(the_queue->front + i) % QUEUE_SIZE].req_id);
        if (i < the_queue->size - 1) {
            printf(",");
        }
    }
    printf("]\n");
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
}


/* Main logic of the worker thread */
int worker_main (void* arg){
	struct timespec start_time, receipt_time, completion_time;
	struct worker_params* worker_param = (struct worker_params*)arg;
	struct queue* the_queue = worker_param->the_queue;
	int conn_socket = worker_param->conn_socket;

    clock_gettime(CLOCK_REALTIME, &start_time);
	
	
	while(1){
		struct request my_request = get_from_queue(the_queue);
		get_elapsed_busywait(my_request.req_length.tv_sec, my_request.req_length.tv_nsec);
		dump_queue_status(the_queue);
		
		size_t send_response = send(conn_socket, &my_request.req_id, sizeof(my_request.req_id), 0);
		if (send_response == -1) {
			perror("Error sending response \n");
			break;
		}else{
			clock_gettime(CLOCK_MONOTONIC, &completion_time);
		}

		printf("R%lu:%ld.%09ld,%ld.%09ld,%ld.%09ld,%ld.%09ld,%ld.%09ld\n", 
		my_request.req_id, my_request.req_timestamp.tv_sec, my_request.req_timestamp.tv_nsec, my_request.req_length.tv_sec, my_request.req_length.tv_nsec, 0, 0, start_time.tv_sec, start_time.tv_nsec ,completion_time.tv_sec,completion_time.tv_nsec);

	}

	return EXIT_SUCCESS;
}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket)
{
	struct request * req;
	struct queue * the_queue = (struct queue*)malloc(sizeof(struct queue));
	struct timespec receipt_time;
	size_t in_bytes;
	void* child_stack = malloc(STACK_SIZE);

	struct worker_params* worker_param = malloc(sizeof(struct worker_params));
	worker_param->conn_socket = conn_socket;
	worker_param->the_queue = the_queue;

	/* The connection with the client is alive here. Let's
	 * initialize the shared queue. */

	initialize_queue(the_queue);

	/* Queue ready to go here. Let's start the worker thread. */

	if(child_stack == NULL){
		perror("ERROR: Unable to allocate memory.\n");
      	exit(EXIT_FAILURE);
	}


	int pid = clone(worker_main, child_stack + STACK_SIZE, CLONE_THREAD | CLONE_VM | CLONE_SIGHAND | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM, worker_param );

	if (pid < 0) {
        perror("ERROR: Unable to create the child process.\n");
        exit(EXIT_FAILURE);
    }

	/* We are ready to proceed with the rest of the request
	 * handling logic. */

	req = (struct request *)malloc(sizeof(struct request));

	do {
		in_bytes = recv(conn_socket, req, sizeof(struct request), 0);

		clock_gettime(CLOCK_MONOTONIC, &receipt_time);

		/* SAMPLE receipt_timestamp HERE */

		/* Don't just return if in_bytes is 0 or -1. Instead
		 * skip the response and break out of the loop in an
		 * orderly fashion so that we can de-allocate the req
		 * and resp varaibles, and shutdown the socket. */
		if (in_bytes > 0) {
			add_to_queue(*req, the_queue);
		}
		// else{
		// 	break;
		// }
	} while (in_bytes > 0);

	/* PERFORM ORDERLY DEALLOCATION AND OUTRO HERE */
}


/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;

	/* Get port to bind our socket to */
	if (argc > 1) {
		socket_port = strtol(argv[1], NULL, 10);
		printf("INFO: setting server port as: %d\n", socket_port);
	} else {
		ERROR_INFO();
		fprintf(stderr, USAGE_STRING, argv[0]);
		return EXIT_FAILURE;
	}

	/* Now onward to create the right type of socket */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		ERROR_INFO();
		perror("Unable to create socket");
		return EXIT_FAILURE;
	}

	/* Before moving forward, set socket to reuse address */
	optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval));

	/* Convert INADDR_ANY into network byte order */
	any_address.s_addr = htonl(INADDR_ANY);

	/* Time to bind the socket to the right port  */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(socket_port);
	addr.sin_addr = any_address;

	/* Attempt to bind the socket with the given parameters */
	retval = bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to bind socket");
		return EXIT_FAILURE;
	}

	/* Let us now proceed to set the server to listen on the selected port */
	retval = listen(sockfd, BACKLOG_COUNT);

	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to listen on socket");
		return EXIT_FAILURE;
	}

	/* Ready to accept connections! */
	printf("INFO: Waiting for incoming connection...\n");
	client_len = sizeof(struct sockaddr_in);
	accepted = accept(sockfd, (struct sockaddr *)&client, &client_len);

	if (accepted == -1) {
		ERROR_INFO();
		perror("Unable to accept connections");
		return EXIT_FAILURE;
	}

	/* Initialize queue protection variables. DO NOT TOUCH. */
	queue_mutex = (sem_t *)malloc(sizeof(sem_t));
	queue_notify = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(queue_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue mutex");
		return EXIT_FAILURE;
	}
	retval = sem_init(queue_notify, 0, 0);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue notify");
		return EXIT_FAILURE;
	}
	/* DONE - Initialize queue protection variables. DO NOT TOUCH */

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted);

	free(queue_mutex);
	free(queue_notify);

	close(sockfd);
	return EXIT_SUCCESS;

}
