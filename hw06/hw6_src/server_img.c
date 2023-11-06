/*******************************************************************************
* Single-Threaded FIFO Image Server Implementation w/ Queue Limit
*
* Description:
*     A server implementation designed to process client
*     requests for image processing in First In, First Out (FIFO)
*     order. The server binds to the specified port number provided as
*     a parameter upon launch. It launches a secondary thread to
*     process incoming requests and allows to specify a maximum queue
*     size.
*
* Usage:
*     <build directory>/server -q <queue_size> -w <workers> -p <policy> <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*     queue_size  - The maximum number of queued requests.
*     workers     - The number of parallel threads to process requests.
*     policy      - The queue policy to use for request dispatching.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     October 31, 2023
*
* Notes:
*     Ensure to have proper permissions and available port before running the
*     server. The server relies on a FIFO mechanism to handle requests, thus
*     guaranteeing the order of processing. If the queue is full at the time a
*     new request is received, the request is rejected with a negative ack.
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
	"Usage: %s -q <queue size> "		\
	"-w <workers: 1> "			\
	"-p <policy: FIFO> "			\
	"<port_number>\n"

/* 4KB of stack for the worker thread */
#define STACK_SIZE (4096)

/* Mutex needed to protect the threaded printf. DO NOT TOUCH */
sem_t * printf_mutex;

/* Synchronized printf for multi-threaded operation */
#define sync_printf(...)			\
	do {					\
		sem_wait(printf_mutex);		\
		printf(__VA_ARGS__);		\
		sem_post(printf_mutex);		\
	} while (0)

/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */

struct request_meta {
	struct request request;
	struct timespec receipt_timestamp;
	struct timespec start_timestamp;
	struct timespec completion_timestamp;
};

enum queue_policy {
	QUEUE_FIFO,
	QUEUE_SJN
};

struct queue {
	size_t wr_pos;
	size_t rd_pos;
	size_t max_size;
	size_t available;
	enum queue_policy policy;
	struct request_meta * requests;
};

struct connection_params {
	size_t queue_size;
	size_t workers;
	enum queue_policy queue_policy;
};

struct worker_params {
	int conn_socket;
	int worker_done;
	struct queue * the_queue;
	int worker_id;
};

enum worker_command {
	WORKERS_START,
	WORKERS_STOP
};

struct ImageInfo {
	struct image* img; 
	uint32_t id;
};
#define MAX_IMAGES 100
struct ImageInfo imageArray[MAX_IMAGES];

uint64_t image_id_counter = 0;

void generate_image_id() {
    image_id_counter++; 
}


void queue_init(struct queue * the_queue, size_t queue_size, enum queue_policy policy)
{
	the_queue->rd_pos = 0;
	the_queue->wr_pos = 0;
	the_queue->max_size = queue_size;
	the_queue->requests = (struct request_meta *)malloc(sizeof(struct request_meta)
						     * the_queue->max_size);
	the_queue->available = queue_size;
	the_queue->policy = policy;
}

/* Add a new request <request> to the shared queue <the_queue> */
int add_to_queue(struct request_meta to_add, struct queue * the_queue)
{
	int retval = 0;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* Make sure that the queue is not full */
	if (the_queue->available == 0) {
		retval = 1;
	} else {
		/* If all good, add the item in the queue */
		the_queue->requests[the_queue->wr_pos] = to_add;
		the_queue->wr_pos = (the_queue->wr_pos + 1) % the_queue->max_size;
		the_queue->available--;
		/* QUEUE SIGNALING FOR CONSUMER --- DO NOT TOUCH */
		sem_post(queue_notify);
	}

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Add a new request <request> to the shared queue <the_queue> */
struct request_meta get_from_queue(struct queue * the_queue)
{
	struct request_meta retval;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_notify);
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */
	retval = the_queue->requests[the_queue->rd_pos];
	the_queue->rd_pos = (the_queue->rd_pos + 1) % the_queue->max_size;
	the_queue->available++;

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

void dump_queue_status(struct queue * the_queue)
{
	size_t i, j;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */
	sync_printf("Q:[");

	for (i = the_queue->rd_pos, j = 0; j < the_queue->max_size - the_queue->available;
	     i = (i + 1) % the_queue->max_size, ++j)
	{
		sync_printf("R%ld%s", the_queue->requests[i].request.req_id,
		       ((j+1 != the_queue->max_size - the_queue->available)?",":""));
	}

	sync_printf("]\n");

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
}

/* Main logic of the worker thread */
int worker_main (void * arg)
{
	struct timespec now;
	struct worker_params * params = (struct worker_params *)arg;

	/* Print the first alive message. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	sync_printf("[#WORKER#] %lf Worker Thread Alive!\n", TSPEC_TO_DOUBLE(now));

	/* Okay, now execute the main logic. */
	while (!params->worker_done) {
		// sync_printf("in worker main while loop\n");
		struct request_meta req;
		struct response resp;
		if(params->the_queue->available ==  params->the_queue->max_size){
			continue;
		}
		req = get_from_queue(params->the_queue);
		// sync_printf("got request\n");
		uint8_t err;
		struct image *new_img;

		/* Detect wakeup after termination asserted */
		if (params->worker_done)
			break;

		clock_gettime(CLOCK_MONOTONIC, &req.start_timestamp);

		/* IMPLEMENT ME! Take the necessary steps to process
		 * the client's request for image processing. */
		 switch(req.request.img_op) {
            case IMG_ROT90CLKW:
				// sync_printf("in IMG_ROT90CLKW\n");
                // Rotate image or its copy based on overwrite
                if(req.request.overwrite == 1) {
                    new_img = rotate90Clockwise(imageArray[image_id_counter].img,err);
					imageArray[image_id_counter].img = new_img;
                    resp.img_id = req.request.img_id;
                } else {
                    struct image *new_img = cloneImage(imageArray[image_id_counter].img,err); // Create a copy for modification
                    new_img = rotate90Clockwise(new_img,err);
                    // Store the new image and ID
                    imageArray[image_id_counter].img = new_img;
                    resp.img_id = image_id_counter; // Send new image ID back
                }
                break;
            case IMG_BLUR:
				// sync_printf("in IMG_BLUR\n");
                if(req.request.overwrite == 1) {
                    new_img = blurImage(imageArray[image_id_counter].img);
					imageArray[image_id_counter].img = new_img;
                    resp.img_id = req.request.img_id;
                } else {
                    struct image *new_img = cloneImage(imageArray[image_id_counter].img,err); // Create a copy for modification
                    new_img = blurImage(new_img);
                    imageArray[image_id_counter].img = new_img;
					resp.img_id = image_id_counter; // Send new image ID back
                }
                break;
			case IMG_SHARPEN:
				// sync_printf("in IMG_SHARPEN\n");
				if(req.request.overwrite == 1) {
                    new_img = sharpenImage(imageArray[image_id_counter].img);
					imageArray[image_id_counter].img = new_img;
                    resp.img_id = req.request.img_id;
                } else {
                    struct image *new_img = cloneImage(imageArray[image_id_counter].img,err); // Create a copy for modification
                    sharpenImage(new_img);
                    // Store the new image and ID
                    imageArray[image_id_counter].img = new_img;
					resp.img_id = image_id_counter; // Send new image ID back
                }
				break;
			case IMG_VERTEDGES:
				// sync_printf("in IMG_VERTEDGES\n");
				if(req.request.overwrite == 1) {
                    new_img = detectVerticalEdges(imageArray[image_id_counter].img);
					imageArray[image_id_counter].img = new_img;
                    resp.img_id = req.request.img_id;
                } else {
                    struct image *new_img = cloneImage(imageArray[image_id_counter].img,err); // Create a copy for modification
                    detectVerticalEdges(new_img);
                    // Store the new image and ID
                    imageArray[image_id_counter].img = new_img;
					resp.img_id = image_id_counter; // Send new image ID back
                }
				break;
			case IMG_HORIZEDGES:
				// sync_printf("in IMG_HORIZEDGES\n");
				if(req.request.overwrite == 1) {
                    new_img = detectHorizontalEdges(imageArray[image_id_counter].img);
					imageArray[image_id_counter].img = new_img;
                    resp.img_id = req.request.img_id;
                } else {
                    struct image *new_img = cloneImage(imageArray[image_id_counter].img,err); // Create a copy for modification
                    detectHorizontalEdges(new_img);
                    imageArray[image_id_counter].img = new_img;
					resp.img_id = image_id_counter; // Send new image ID back
                }
				break;
			case IMG_RETRIEVE:
				// sync_printf("in IMG_RETRIEVE\n");
				resp.img_id = req.request.img_id;
				generate_image_id();
				imageArray[image_id_counter].img = imageArray[image_id_counter-1].img;
				imageArray[image_id_counter].id = image_id_counter;
				break;
			default:
				resp.ack = RESP_REJECTED;
				break;
				
			}

		
		clock_gettime(CLOCK_MONOTONIC, &req.completion_timestamp);

		/* Now provide a response! */
		resp.req_id = req.request.req_id;
		resp.ack = RESP_COMPLETED;

		/* IMPLEMENT ME! Set the img_id field for the response
		 * here, if necessary. */
		send(params->conn_socket, &resp, sizeof(struct response), 0);
		// sync_printf("sent response\n");

		if (req.request.img_op == IMG_RETRIEVE) {
			printf("INFO: Sending image %ld\n", req.request.img_id);
			sendImage(imageArray[image_id_counter].img , params->conn_socket); 
		}

		/* IMPLEMENT ME! Print out the post-processing status
		 * report. */
		sync_printf("T%d R%lu:%lf,%s,%d,%ld,%ld,%lf,%lf,%lf\n", 
			0,
			req.request.req_id, 
			TSPEC_TO_DOUBLE(req.request.req_timestamp),
			__opcode_strings[req.request.img_op],
			req.request.overwrite,
			req.request.img_id,
			image_id_counter,
			TSPEC_TO_DOUBLE(req.receipt_timestamp),
			TSPEC_TO_DOUBLE(req.start_timestamp),
			TSPEC_TO_DOUBLE(req.completion_timestamp)
		);
		// sync_printf("printed lines\n");

		dump_queue_status(params->the_queue);
		// sync_printf("dumped queue status\n");
		// if (image_id_counter && req.request.overwrite == 0) {
        //     deleteImage(imageArray[image_id_counter].img);
        //     imageArray[new_image_id % MAX_IMAGES].img = NULL; // Clear the entry
        // }

        // // Cleanup the received image if it was not supposed to be overwritten
        // if (req.request.overwrite == 1) {
        //     deleteImage(img);
        // }
	}

	return EXIT_SUCCESS;
}

/* This function will start/stop all the worker threads wrapping
 * around the clone()/waitpid() system calls */
int control_workers(enum worker_command cmd, size_t worker_count,
		    struct worker_params * common_params)
{
	/* Anything we allocate should we kept as static for easy
	 * deallocation when the STOP command is issued */
	static char ** worker_stacks = NULL;
	static struct worker_params ** worker_params = NULL;
	static int * worker_ids = NULL;

	/* Start all the workers */
	if (cmd == WORKERS_START) {
		size_t i;
		/* Allocate all stacks and parameters */
		worker_stacks = (char **)malloc(worker_count * sizeof(char *));
		worker_params = (struct worker_params **)
			malloc(worker_count * sizeof(struct worker_params *));
		worker_ids = (int *)malloc(worker_count * sizeof(int));

		if (!worker_stacks || !worker_params) {
			ERROR_INFO();
			perror("Unable to allocate descriptor arrays for threads.");
			return EXIT_FAILURE;
		}

		/* Allocate and initialize as needed */
		for (i = 0; i < worker_count; ++i) {
			worker_ids[i] = -1;

			worker_stacks[i] = malloc(STACK_SIZE);
			worker_params[i] = (struct worker_params *)
				malloc(sizeof(struct worker_params));

			if (!worker_stacks[i] || !worker_params[i]) {
				ERROR_INFO();
				perror("Unable to allocate memory for thread.");
				return EXIT_FAILURE;
			}

			worker_params[i]->conn_socket = common_params->conn_socket;
			worker_params[i]->the_queue = common_params->the_queue;
			worker_params[i]->worker_done = 0;
			worker_params[i]->worker_id = i;
		}

		/* All the allocations and initialization seem okay,
		 * let's start the threads */
		for (i = 0; i < worker_count; ++i) {
			worker_ids[i] = clone(worker_main, worker_stacks[i] + STACK_SIZE,
					      CLONE_THREAD | CLONE_VM | CLONE_SIGHAND |
					      CLONE_FS | CLONE_FILES | CLONE_SYSVSEM,
					      worker_params[i]);

			if (worker_ids[i] < 0) {
				ERROR_INFO();
				perror("Unable to start thread.");
				return EXIT_FAILURE;
			} else {
				sync_printf("INFO: Worker thread %ld (TID = %d) started!\n",
				       i, worker_ids[i]);
			}
		}
	}

	else if (cmd == WORKERS_STOP) {
		size_t i;

		/* Command to stop the threads issues without a start
		 * command? */
		if (!worker_stacks || !worker_params || !worker_ids) {
			return EXIT_FAILURE;
		}

		/* First, assert all the termination flags */
		for (i = 0; i < worker_count; ++i) {
			if (worker_ids[i] < 0) {
				continue;
			}

			/* Request thread termination */
			worker_params[i]->worker_done = 1;
		}

		/* Next, unblock threads and wait for completion */
		for (i = 0; i < worker_count; ++i) {
			if (worker_ids[i] < 0) {
				continue;
			}

			sem_post(queue_notify);
			waitpid(-1, NULL, 0);
			sync_printf("INFO: Worker thread exited.\n");
		}

		/* Finally, do a round of deallocations */
		for (i = 0; i < worker_count; ++i) {
			free(worker_stacks[i]);
			free(worker_params[i]);
		}

		free(worker_stacks);
		worker_stacks = NULL;

		free(worker_params);
		worker_params = NULL;

		free(worker_ids);
		worker_ids = NULL;
	}

	else {
		ERROR_INFO();
		perror("Invalid thread control command.");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket, struct connection_params conn_params)
{
	struct request_meta * req;
	struct queue * the_queue;
	size_t in_bytes;

	/* The connection with the client is alive here. Let's start
	 * the worker thread. */
	struct worker_params common_worker_params;
	int res;

	/* Now handle queue allocation and initialization */
	the_queue = (struct queue *)malloc(sizeof(struct queue));
	queue_init(the_queue, conn_params.queue_size, conn_params.queue_policy);

	common_worker_params.conn_socket = conn_socket;
	common_worker_params.the_queue = the_queue;
	res = control_workers(WORKERS_START, conn_params.workers, &common_worker_params);

	/* Do not continue if there has been a problem while starting
	 * the workers. */
	if (res != EXIT_SUCCESS) {
		free(the_queue);

		/* Stop any worker that was successfully started */
		control_workers(WORKERS_STOP, conn_params.workers, NULL);
		return;
	}

	/* We are ready to proceed with the rest of the request
	 * handling logic. */

	req = (struct request_meta *)malloc(sizeof(struct request_meta));

	do {
		in_bytes = recv(conn_socket, &req->request, sizeof(struct request), 0);
		clock_gettime(CLOCK_MONOTONIC, &req->receipt_timestamp);

		/* Don't just return if in_bytes is 0 or -1. Instead
		 * skip the response and break out of the loop in an
		 * orderly fashion so that we can de-allocate the req
		 * and resp varaibles, and shutdown the socket. */
		if (in_bytes > 0) {

			/* IMPLEMENT ME! Check right away if the
			 * request has img_op set to IMG_REGISTER. If
			 * so, handle the operation right away,
			 * reading in the full image payload, replying
			 * to the server, and bypassing the queue. */
			if (req->request.img_op == IMG_REGISTER) {
				clock_gettime(CLOCK_MONOTONIC, &req->start_timestamp);
				struct image *img = recvImage(conn_socket);
				struct response resp;

				if (img != NULL) {
					generate_image_id();  // Assign an ID to the image

					if (image_id_counter < MAX_IMAGES) {
						imageArray[image_id_counter].img = img;   // Store the image in the array
						imageArray[image_id_counter].id = image_id_counter;  // Store the id in the array

						// Set response data
						resp.req_id = req->request.req_id;
						resp.img_id = image_id_counter;
						resp.ack = RESP_COMPLETED;  // Assuming RESP_ACCEPTED is defined as 0

						clock_gettime(CLOCK_MONOTONIC, &req->completion_timestamp);

						sync_printf("T%d R%lu:%lf,%s,%d,%ld,%ld,%lf,%lf,%lf\n", 
                        0,
                        req->request.req_id, 
                        TSPEC_TO_DOUBLE(req->request.req_timestamp),
                        __opcode_strings[req->request.img_op],
                        req->request.overwrite,
                        req->request.img_id,
                        image_id_counter,
                        TSPEC_TO_DOUBLE(req->receipt_timestamp),
                        TSPEC_TO_DOUBLE(req->start_timestamp),
                        TSPEC_TO_DOUBLE(req->completion_timestamp)
                        );
                    	dump_queue_status(the_queue);

					} else {
						// No space left in the array to store new image
						deleteImage(img);  // Assuming this function frees the image
						resp.req_id = req->request.req_id;
						resp.img_id = 0;  // Or some invalid ID to indicate failure
						resp.ack = RESP_REJECTED;  // Assuming RESP_REJECTED is defined as 1
					}
				} else {
					resp.req_id = req->request.req_id;
					resp.img_id = 0;  // Or some invalid ID to indicate failure
					resp.ack = RESP_REJECTED;  // Assuming RESP_REJECTED is defined as 1
				}

				send(conn_socket, &resp, sizeof(struct response), 0);
				// sync_printf("sent response for img register\n");
				continue;  // Skip the rest of the loop and wait for next request
			}					

			res = add_to_queue(*req, the_queue);
            // sync_printf("added to queue\n");

			/* The queue is full if the return value is 1 */
			if (res) {
				// sync_printf("rejected from queue\n");
				struct response resp;
				/* Now provide a response! */
				resp.req_id = req->request.req_id;
				resp.ack = RESP_REJECTED;
				send(conn_socket, &resp, sizeof(struct response), 0);

				sync_printf("X%ld:%lf,%lf,%lf\n", req->request.req_id,
					TSPEC_TO_DOUBLE(req->request.req_timestamp),
					TSPEC_TO_DOUBLE(req->request.req_length),
					TSPEC_TO_DOUBLE(req->receipt_timestamp)
					);
			}
			
			
		}
	} while (in_bytes > 0);


	/* Stop all the worker threads. */
	control_workers(WORKERS_STOP, conn_params.workers, NULL);

	free(req);
	shutdown(conn_socket, SHUT_RDWR);
	close(conn_socket);
	printf("INFO: Client disconnected.\n");
}


/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval, opt;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;
	struct connection_params conn_params;
	conn_params.queue_size = 0;
	conn_params.queue_policy = QUEUE_FIFO;
	conn_params.workers = 1;

	/* Parse all the command line arguments */
	while((opt = getopt(argc, argv, "q:w:p:")) != -1) {
		switch (opt) {
		case 'q':
			conn_params.queue_size = strtol(optarg, NULL, 10);
			printf("INFO: setting queue size = %ld\n", conn_params.queue_size);
			break;
		case 'w':
			conn_params.workers = strtol(optarg, NULL, 10);
			printf("INFO: setting worker count = %ld\n", conn_params.workers);
			if (conn_params.workers != 1) {
				ERROR_INFO();
				fprintf(stderr, "Only 1 worker is supported in this implementation!\n" USAGE_STRING, argv[0]);
				return EXIT_FAILURE;
			}
			break;
		case 'p':
			if (!strcmp(optarg, "FIFO")) {
				conn_params.queue_policy = QUEUE_FIFO;
			} else {
				ERROR_INFO();
				fprintf(stderr, "Invalid queue policy.\n" USAGE_STRING, argv[0]);
				return EXIT_FAILURE;
			}
			printf("INFO: setting queue policy = %s\n", optarg);
			break;
		default: /* '?' */
			fprintf(stderr, USAGE_STRING, argv[0]);
		}
	}

	if (!conn_params.queue_size) {
		ERROR_INFO();
		fprintf(stderr, USAGE_STRING, argv[0]);
		return EXIT_FAILURE;
	}

	if (optind < argc) {
		socket_port = strtol(argv[optind], NULL, 10);
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

	/* Initilize threaded printf mutex */
	printf_mutex = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(printf_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize printf mutex");
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
	/* DONE - Initialize queue protection variables */

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted, conn_params);

	free(queue_mutex);
	free(queue_notify);

	close(sockfd);
	return EXIT_SUCCESS;
}
