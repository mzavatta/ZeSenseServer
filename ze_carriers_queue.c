//unblocking implementation both get and put


#include "ze_carriers_queue.h"
#include "utlist.h"

//ASensorEventQueue_getEvents(mngr->sensorEventQueue, &event, 1)


/* Memory for the event parameter should already be allocated! */
int get_carrier_event(ze_carriers_queue_t *queue, ASensorEvent *event) {

	/* Here I have the choice of creating a new item or just pass
	 * it back by value.
	 * I would prefer allocating a new space for it but still..
	 */

	int return_value = 0;

	pthread_mutex_lock(&(queue->mtx));
		if (queue->head == NULL) {//queue->counter <= 0) { //empty
			/*
			 * pthread_cond_wait(queue->notempty, &(queue->mtx));
			 * do nothing, we must not block!
			 */
			return_value = 0; //cannot return immediately here, need to unlock mutex
		}
		else {
			//memcopy the first element inside the event parameter
			memcpy(event, &(queue->head->event), sizeof(ASensorEvent));
			//unplug the first element
			ze_cq_elem_t *temp = queue->head;
			queue->head = queue->head->next;
			//free the first element
			free(temp);
			return_value = 1;
			//queue->counter--;
			//pthread_cond_signal(&(buf->notfull)); //surely no longer full
		}
	pthread_mutex_unlock(&(queue->mtx));

	return return_value;
}


int put_carrier_event(ze_carriers_queue_t *queue, ASensorEvent event) {

	LOGW("Putting event in Carrier Queue");

	int return_value = 0;

	/* Important that the element has next = null because we check the empty
	 * condition on that. */
	ze_cq_elem_t *toinsert = malloc(sizeof(ze_cq_elem_t));
	if(toinsert==NULL) return 0;
	memset(toinsert, 0, sizeof(ze_cq_elem_t));
	toinsert->next = NULL;

	pthread_mutex_lock(&(queue->mtx));
		/*
		if (buf->counter >= SM_RBUF_SIZE) { //full (greater shall not happen)
			LOGI("Found full");
			pthread_cond_wait(&(buf->notfull), &(buf->mtx));
			LOGI("Not full anymore, condvar became true");
		}
		*/

		//copy contents
		toinsert->event = event;

		//append the element
		LL_APPEND(queue->head, toinsert);
		/*
		buf->rbuf[buf->puthere].rtype = rtype;
		buf->rbuf[buf->puthere].sensor = sensor;
		buf->rbuf[buf->puthere].ticket = ticket;
		buf->rbuf[buf->puthere].freq = freq;
		buf->puthere = ((buf->puthere)+1) % SM_RBUF_SIZE;
		buf->counter++;
		//pthread_cond_signal(buf->notempty); //surely no longer empty
		 */
	pthread_mutex_unlock(&(queue->mtx));

	return 1;
}

ze_carriers_queue_t* init_carriers_queue() {

	ze_carriers_queue_t *queue = malloc(sizeof(ze_carriers_queue_t));
	if (queue == NULL) return NULL;
	memset(queue, 0, sizeof(ze_carriers_queue_t));
	queue->head = NULL;

	//memset(buf->rbuf, 0, SM_RBUF_SIZE*sizeof(ze_sm_request_t));

	/* What happens if a thread tries to initialize a mutex or a cond var
	 * that has already been initialized? "POSIX explicitly
	 * states that the behavior is not defined, so avoid
	 * this situation in your programs"
	 */
	int error = pthread_mutex_init(&(queue->mtx), NULL);
	if (error) {
		LOGW("Failed to initialize mtx:%s\n", strerror(error));
		return NULL;
	}
	/*error = pthread_cond_init(&(queue->notfull), NULL);
	if (error)
		LOGW("Failed to initialize full cond var:%s\n", strerror(error));*/
	/*
	 * error = pthread_cond_init(buf->notempty, NULL);
	 * if (error)
	 *	 fprintf(stderr, "Failed to initialize empty cond var:%s\n", strerror(error));
	 */

	/* Reset indexes */
	/*buf->gethere = 0;
	buf->puthere = 0;
	buf->counter = 0;*/

	return queue;
}

