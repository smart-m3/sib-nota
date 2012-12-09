
#include <glib.h>
#include <string.h>
#include <whiteboard_log.h>
#include <h_in/h_bsdapi.h>

#include "sib_handler.h"
#include "SibAccessNota_service.h"
#include "sib_maxfd.h"
#include "kp_server.h"

#define MAX_THREADS 10

#define TEST_PORT 0
#define BACKLOG 10     // how many pending connections queue will hold


struct _KPListener
{
  sid_t sid;
  SIBMaxFd *maxfd;
  GHashTable *server_map; // fd -> KPServer
  //  GThreadPool *threadpool; // thread pool for kp_handler
  GSList *open_sockets; /* client sockets */
  GSList *error_sockets;
  int lfd; /* listen socket */
  //  GAsyncQueue *reqqueue;
  //GAsyncQueue *rspqueue;
};

static gpointer kp_listener_thread(gpointer data);
//static void kp_handler_thread(gpointer data, gpointer user_data);
//static gint receiver(KPHandlerArgs *args);

KPListener *kp_listener_new(sid_t sid, SIBMaxFd *maxfd)
{
  GThread *listener_thread = NULL;
  KPListener *self = g_new0(KPListener,1);
  GError *err=NULL;

  self->sid = sid;
  self->maxfd = maxfd;
  //  self->reqqueue = g_async_queue_new();
  //self->rspqueue = g_async_queue_new();

  self->server_map = g_hash_table_new( g_direct_hash, g_direct_equal );

  //  self->threadpool = g_thread_pool_new( kp_handler_thread,
  //					self,
  //					MAX_THREADS,
  //					FALSE,
  //					&err);
  //if(err)
  // {
  //   whiteboard_log_debug("Could not create threadpool, err: %s\n", err->message);
  //   g_error_free(err);
  //   g_free(self);
  //  self= NULL;
  //  }
  //else
  //  {
      
      listener_thread = g_thread_create( kp_listener_thread,
					 self,
					 FALSE,
					 &err);
      if(err)
	{
	  whiteboard_log_debug("Could not create listener thread, err: %s\n", err->message);
	  g_error_free(err);
	  g_free(self);
	  self= NULL;
	}
      // }
  return self;
}

gpointer kp_listener_thread(gpointer data)
{
  KPListener *self = (KPListener *)data;
  int new_fd;  // listen on self->lfd, new connection on new_fd

  nota_addr_t addr = {self->sid, TEST_PORT};
  h_in_t* core = Hgetinstance();
  gboolean error_found = FALSE;
  fd_set read_set;
  fd_set error_set;
  //struct timeval tv;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  whiteboard_log_debug("listener: creating socket for sid: %d\n", self->sid);
  if ((self->lfd = Hsocket(core, AF_NOTA, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(1);
  }
  
  sib_maxfd_set( self->maxfd, self->lfd);
  
  whiteboard_log_debug("listener: binding socket(%d)\n",  self->lfd);
  if (Hbind(core, self->lfd, (struct sockaddr *)&addr, sizeof (addr)) < 0) 
    {
      whiteboard_log_debug("Error with bind: %s", strerror(errno));
      Hclose(core,self->lfd);
      exit(1);
    }

  whiteboard_log_debug("listener: listening socket(%d)\n",  self->lfd);
  if (Hlisten(core, self->lfd, BACKLOG) < 0) 
    {
      perror("listen");
      Hclose(core,self->lfd);
      exit(1);
    }

  //  g_async_queue_ref(self->reqqueue);
  //g_async_queue_ref(self->rspqueue);
  
  whiteboard_log_debug("listener: entering accept() loop for socket: %d, core: %p\n", self->lfd, core);
  while( !error_found  ) 
    {  // main accept() loop
      GSList *link = NULL;
      gint maxfd;
      gint err;
      //      gpointer req = NULL;
      new_fd = -1;
      FD_ZERO( &read_set);
      FD_ZERO( &error_set);
      FD_SET( self->lfd, &read_set);
      FD_SET( self->lfd, &error_set);

      //     if( (req = g_async_queue_try_pop(self->reqqueue)) != NULL)
      //	{
      //whiteboard_log_debug("Got Quit request\n");
      //  g_async_queue_push(self->rspqueue, GINT_TO_POINTER(1));
      //  break;
      //}

      link  = self->open_sockets;
      while(link)
	{
	  FD_SET( GPOINTER_TO_INT(link->data), &read_set);
	  FD_SET( GPOINTER_TO_INT(link->data), &error_set);
	  link = g_slist_next(link);
	}
      //tv.tv_sec = 0;
      //tv.tv_usec = 100;
      maxfd = sib_maxfd_lock_and_get( self->maxfd);
      err = Hselect(core, maxfd+1, &read_set, NULL, &error_set, NULL);
      sib_maxfd_unlock(self->maxfd);
      // Go throgh KP sockets
      if( err < 0 )
	{
	  whiteboard_log_warning("Error with Hselect: %s", strerror(errno));
	  error_found = TRUE;
	  break;
	}
      else
	{
	  for( link = self->open_sockets; link; link=g_slist_next(link))
	    {
	      int fd = GPOINTER_TO_INT(link->data);
	      if( FD_ISSET( fd, &error_set) )
		{
		  whiteboard_log_debug("Socket %d: disconnected\n", fd);
		  self->error_sockets = g_slist_prepend(self->error_sockets, GINT_TO_POINTER(fd));
		}
	      else if( FD_ISSET(fd, &read_set) )
		{
		  KPServer *server = g_hash_table_lookup(self->server_map, GINT_TO_POINTER(fd));
		  if(server)
		    {
		      kp_server_handle_incoming(server);
		    }
		  else
		    {
		      whiteboard_log_warning("Hselect for socket %d, but matching KPServer not found\n", fd);
		    }
		}
	    }
	  //Also check the listening socket
	  if( !error_found && FD_ISSET( self->lfd, &error_set))
	    {
	      whiteboard_log_warning("Error on listening socket: %s\n",strerror(errno));
	      error_found = TRUE;
	    }
	  else if( !error_found && FD_ISSET( self->lfd, &read_set) )
	    {
	      kpserver = NULL;
	      new_fd = Haccept(core, self->lfd, NULL, NULL);
	      if (new_fd  < 0) 
		{
		  whiteboard_log_warning("Error with listening socket Haccept: %s", strerror(errno));
		  error_found = TRUE;
		}
	      else
		{
		  whiteboard_log_debug("Accepted %d\n", new_fd);
		  
		  kpserver = kp_server_new(new_fd, self);
		  if(kpserver)
		    {
		      sib_maxfd_set( self->maxfd, new_fd);
		      g_hash_table_insert( self->server_map, GINT_TO_POINTER(new_fd), (gpointer)kpserver);
		    }
		  else
		    {
		      whiteboard_log_warning("Could not create new KPServer structure\n");
		      new_fd = -1;
		    }
		}
	    }
	  
	  while(self->error_sockets)
	    {
	      int fd;
	      link = self->error_sockets;
	      fd = GPOINTER_TO_INT(link->data);
	      whiteboard_log_debug("Removing socket %d from select list.\n", fd);
	      self->error_sockets = g_slist_remove_link(self->error_sockets, link);
	      self->open_sockets = g_slist_remove( self->open_sockets, GINT_TO_POINTER(fd));
	      kpserver = g_hash_table_lookup( self->server_map, GINT_TO_POINTER(fd));
	      if(kpserver)
		{
		  g_hash_table_remove(self->server_map, GINT_TO_POINTER(fd));
		  kp_server_destroy(kpserver);
		  whiteboard_log_debug("Destroying server w/ fd %d\n", fd);
		}
	    }

	  if( !error_found && new_fd > 0)
	    {
	      self->open_sockets = g_slist_prepend(self->open_sockets, GINT_TO_POINTER(new_fd));
	    }	  
	}
    }
  //  g_async_queue_unref(self->reqqueue);
  //g_async_queue_unref(self->rspqueue);
  whiteboard_log_debug_fe();
  return NULL;
}

void kp_listener_destroy(KPListener *self)
{
  //  gpointer rsp;
  GSList *link = NULL;
  KPServer *server = NULL;
  whiteboard_log_debug_fb();
  //  g_async_queue_push(self->reqqueue, GINT_TO_POINTER(1));
  //rsp = g_async_queue_pop(self->rspqueue);
  link = self->open_sockets;
  while(link)
    {
      server = g_hash_table_lookup(self->server_map, link->data);
      g_hash_table_remove(self->server_map, link->data);
      if(server)
	{
	  kp_server_destroy(server);
	}
      link = g_slist_next(link);
    }

  // close also listening socket
  Hclose(Hgetinstance(), self->lfd);

  g_hash_table_destroy(self->server_map);
  //  g_async_queue_unref(self->reqqueue);
  //g_async_queue_unref(self->rspqueue);

  g_free(self);
  whiteboard_log_debug_fe();
}

#if 0
static void kp_handler_thread(gpointer data, gpointer user_data)
{
  int err = 0;
  KPHandlerArgs *args = (KPHandlerArgs *)data;
  h_in_t *core = Hgetinstance();
  whiteboard_log_debug("starting receiver for fd %d\n", args->socket);
  while(err >= 0)
    {
      err = receiver( args);
      //usleep(10);
    }
  
  err = SibAccessNota_service_remove_connection( args->cp);
  if(err==0)
    wsdlc_context_pointer_free( args->cp);
  
  Hclose(core, args->socket);
  g_free(args);
  whiteboard_log_debug("Exiting node_handler\n");
}

gint receiver(KPHandlerArgs *args)
{
  
  gint err = -1;
  int maxfd;
  int new_fd = (int)(args->socket);

  fd_set open_sockets;
  fd_set error_set;
  h_in_t* core = Hgetinstance();

  //  struct timeval timeout;
  //timeout.tv_sec = 0;
  //timeout.tv_usec = 0;
  
  FD_ZERO(&open_sockets);
  FD_ZERO(&error_set);
  FD_SET(new_fd, &open_sockets);
  FD_SET(new_fd, &error_set);

  //  whiteboard_log_debug("node_handler: entering select loop, fd: %d, core: %p\n", new_fd, core);
  //g_mutex_lock(args->mutex);
  maxfd = sib_maxfd_lock_and_get( args->maxfd);
  err = Hselect(core, maxfd+1, &open_sockets, NULL, &error_set, NULL);
  sib_maxfd_unlock(args->maxfd);
  //g_mutex_unlock(args->mutex);
  if(err < 0)
    {
      perror("select: ");
    }
  else if(err > 0 )
    {
      if(FD_ISSET(new_fd, &error_set))
	{
	  whiteboard_log_debug("Error on socket\n");
	}
      else if(FD_ISSET(new_fd, &open_sockets))	{
	  err = wsdlc_handle_incoming(args->cp);
	  if(err < 0)
	    {
	      perror("Receive:");
	      
	      whiteboard_log_debug("Closing client(%d)\n",err);
	    }
	}
    }
  else
    {
      // timeout
    }
  return err;
}
#endif

// Skeleton prototypes. User must implement these functions.
void SibAccessNota_Join_req_process( struct context_pointer* context, uint8_t* nodeid, uint16_t nodeid_len, uint8_t* spaceid, uint16_t spaceid_len, uint32_t msgnum,  uint8_t* credentials, uint16_t credentials_len )
{
  KPListener *listener = (KPListener *)context->user_pointer;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  kpserver = g_hash_table_lookup(listener->server_map, GINT_TO_POINTER(context->socket_id));
  if(kpserver)
    {
      guchar *gnodeid = (guchar *)g_strndup( (gchar *) nodeid, (gint) nodeid_len);
      guchar *gspaceid = (guchar *)g_strndup( (gchar *) spaceid, (gint) spaceid_len);
      guchar *gcredentials = (guchar *)g_strndup( (gchar *) credentials, (gint) credentials_len);
      whiteboard_log_debug("JoinReq\n");
       whiteboard_log_debug("\tspaceid: %s\n",gspaceid);
       whiteboard_log_debug("\tnodeid: %s\n",gnodeid);
       whiteboard_log_debug("\tmsgnum: %d\n", msgnum);      

      kp_server_send_join(kpserver, gspaceid, gnodeid, (gint) msgnum, gcredentials);
      
      g_free(gnodeid);
      g_free(gspaceid);
      g_free(gcredentials);
    }
  else
    {
      whiteboard_log_warning("KPServer w/ fd :%d not found\n", context->socket_id);
    }
  whiteboard_log_debug_fe();
}

void SibAccessNota_Insert_req_process( struct context_pointer* context, uint8_t* nodeid, uint16_t nodeid_len, uint8_t* spaceid, uint16_t spaceid_len, uint32_t msgnum, encoding_t encoding, uint8_t* insert_graph, uint16_t insert_graph_len, confirm_t confirm )
{
  KPListener *listener = (KPListener *)context->user_pointer;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  kpserver = g_hash_table_lookup(listener->server_map, GINT_TO_POINTER(context->socket_id));
  if(kpserver)
    {
      guchar *gnodeid = (guchar *)g_strndup( (gchar *) nodeid, (gint) nodeid_len);
      guchar *gspaceid = (guchar *)g_strndup( (gchar *) spaceid, (gint) spaceid_len);
      guchar *ginsert_graph = (guchar *)g_strndup( (gchar *) insert_graph, (gint) insert_graph_len);
      EncodingType type = EncodingInvalid;
      switch(encoding)
	{
	case ENC_RDF_M3:
	  type = EncodingM3XML;
	  break;
	case ENC_RDF_XML:
	  type = EncodingRDFXML;
	  break;
	default:
	  whiteboard_log_warning("Unknown encoding type\n");
	}
      whiteboard_log_debug("InsertReq\n");
      whiteboard_log_debug("\tspaceid: %s\n",gspaceid);
      whiteboard_log_debug("\tnodeid: %s\n",gnodeid);
      whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
      whiteboard_log_debug("\tigraph: %s\n",ginsert_graph);
      kp_server_send_insert(kpserver, gspaceid, gnodeid, (gint) msgnum, type, ginsert_graph);
      
      g_free(gnodeid);
      g_free(gspaceid);
      g_free(ginsert_graph);
    }
  else
    {
      whiteboard_log_warning("KPServer w/ fd :%d not found\n", context->socket_id);
    }
  whiteboard_log_debug_fe();
}

void SibAccessNota_Update_req_process( struct context_pointer* context, uint8_t* nodeid, uint16_t nodeid_len, uint8_t* spaceid, uint16_t spaceid_len, uint32_t msgnum, encoding_t encoding,  uint8_t* insert_graph, uint16_t insert_graph_len, uint8_t* remove_graph, uint16_t remove_graph_len, confirm_t confirm )
{
  KPListener *listener = (KPListener *)context->user_pointer;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  kpserver = g_hash_table_lookup(listener->server_map, GINT_TO_POINTER(context->socket_id));
  if(kpserver)
    {
      guchar *gnodeid = (guchar *)g_strndup( (gchar *) nodeid, (gint) nodeid_len);
      guchar *gspaceid = (guchar *)g_strndup( (gchar *) spaceid, (gint) spaceid_len);
      guchar *ginsert_graph = (guchar *)g_strndup( (gchar *) insert_graph, (gint) insert_graph_len);
      guchar *gremove_graph = (guchar *)g_strndup( (gchar *) remove_graph, (gint) remove_graph_len);
      EncodingType type = EncodingInvalid;
      switch(encoding)
	{
	case ENC_RDF_M3:
	  type = EncodingM3XML;
	  break;
	case ENC_RDF_XML:
	  type = EncodingRDFXML;
	  break;
	default:
	  whiteboard_log_warning("Unknown encoding type\n");
	}
       whiteboard_log_debug("UpdateReq\n");
      whiteboard_log_debug("\tspaceid: %s\n",gspaceid);
      whiteboard_log_debug("\tnodeid: %s\n",gnodeid);
      whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
      whiteboard_log_debug("\tigraph: %s\n",ginsert_graph);
      whiteboard_log_debug("\trgraph: %s\n",gremove_graph);
      kp_server_send_update(kpserver, gspaceid, gnodeid, (gint) msgnum, type, ginsert_graph, gremove_graph);
      
      g_free(gnodeid);
      g_free(gspaceid);
      g_free(ginsert_graph);
      g_free(gremove_graph);
    }
  else
    {
      whiteboard_log_warning("KPServer w/ fd :%d not found\n", context->socket_id);
    }
  whiteboard_log_debug_fe();
}

void SibAccessNota_Remove_req_process( struct context_pointer* context, uint8_t* nodeid, uint16_t nodeid_len, uint8_t* spaceid, uint16_t spaceid_len, uint32_t msgnum, encoding_t encoding, uint8_t* remove_graph, uint16_t remove_graph_len, confirm_t confirm )
{
  KPListener *listener = (KPListener *)context->user_pointer;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  kpserver = g_hash_table_lookup(listener->server_map, GINT_TO_POINTER(context->socket_id));
  if(kpserver)
    {
      guchar *gnodeid = (guchar *)g_strndup( (gchar *) nodeid, (gint) nodeid_len);
      guchar *gspaceid = (guchar *)g_strndup( (gchar *) spaceid, (gint) spaceid_len);
      guchar *gremove_graph = (guchar *)g_strndup( (gchar *) remove_graph, (gint) remove_graph_len);
      EncodingType type = EncodingInvalid;
      switch(encoding)
	{
	case ENC_RDF_M3:
	  type = EncodingM3XML;
	  break;
	case ENC_RDF_XML:
	  type = EncodingRDFXML;
	  break;
	default:
	  whiteboard_log_warning("Unknown encoding type\n");
	}
      whiteboard_log_debug("RemoveReq\n");
      whiteboard_log_debug("\tspaceid: %s\n",gspaceid);
      whiteboard_log_debug("\tnodeid: %s\n",gnodeid);
      whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
      whiteboard_log_debug("\trgraph: %s\n",gremove_graph);
      kp_server_send_remove(kpserver, gspaceid, gnodeid, (gint) msgnum, type, gremove_graph);
      
      g_free(gnodeid);
      g_free(gspaceid);
      g_free(gremove_graph);
    }
  else
    {
      whiteboard_log_warning("KPServer w/ fd :%d not found\n", context->socket_id);
    }
  whiteboard_log_debug_fe();
}

void SibAccessNota_Query_req_process( struct context_pointer* context, uint8_t* nodeid, uint16_t nodeid_len, uint8_t* spaceid, uint16_t spaceid_len, uint32_t msgnum, query_format_t query_format, uint8_t* query, uint16_t query_len, confirm_t confirm )
{
  KPListener *listener = (KPListener *)context->user_pointer;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  kpserver = g_hash_table_lookup(listener->server_map, GINT_TO_POINTER(context->socket_id));
  if(kpserver)
    {
      guchar *gnodeid = (guchar *)g_strndup( (gchar *) nodeid, (gint) nodeid_len);
      guchar *gspaceid = (guchar *)g_strndup( (gchar *) spaceid, (gint) spaceid_len);
      guchar *gquery = (guchar *)g_strndup( (gchar *) query, (gint) query_len);
      QueryType type;
      switch( query_format)
	{
	case QFORMAT_RDF_M3:
	  type = QueryTypeTemplate;
	  break;
	case QFORMAT_WQL_VALUES:
	  type = QueryTypeWQLValues;
	  break;
	case QFORMAT_WQL_NODETYPES:
	  type = QueryTypeWQLNodeTypes;
	  break;
	case QFORMAT_WQL_RELATED:
	  type = QueryTypeWQLRelated;
	  break;
	case QFORMAT_WQL_ISTYPE:
	  type = QueryTypeWQLIsType;
	  break;
	case QFORMAT_WQL_ISSUBTYPE:
	  type = QueryTypeWQLIsSubType;
	  break;
	case QFORMAT_SPARQL:
	  type = QueryTypeTemplate;
	  break;
	default:
	  type = -1;
	  break;
	}

       whiteboard_log_debug("QueryReq\n");
       whiteboard_log_debug("\tspaceid: %s\n",gspaceid);
       whiteboard_log_debug("\tnodeid: %s\n",gnodeid);
       whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
       whiteboard_log_debug("\ttype: %d\n", type);
       whiteboard_log_debug("\tqgraph: %s\n",gquery);
       
      if(type<0)
	{
	  guchar *results = (guchar *)g_strdup("\n");
	  SibAccessNota_Query_cnf(context,
				  (uint8_t *) gnodeid,
				  (uint16_t) strlen( (gchar *)gnodeid),
				  (uint8_t *) gspaceid,
				  (uint16_t) strlen( (gchar *)gspaceid),
				  (uint16_t) msgnum,
				  SSStatus_KP_Request,
				  (uint8_t *) results,
				  (uint16_t) strlen( (gchar *)results));
	  g_free(results);
	}
      else
	{
	  kp_server_send_query(kpserver, gspaceid, gnodeid, (gint) msgnum, type, gquery);
	}
      g_free(gnodeid);
      g_free(gspaceid);
      g_free(gquery);
    }
  else
    {
      whiteboard_log_warning("KPServer w/ fd :%d not found\n", context->socket_id);
    }
  whiteboard_log_debug_fe();
}

void SibAccessNota_Subscribe_req_process( struct context_pointer* context, uint8_t* nodeid, uint16_t nodeid_len, uint8_t* spaceid, uint16_t spaceid_len, uint32_t msgnum, query_format_t query_format, uint8_t* query, uint16_t query_len, confirm_t results )
{
  KPListener *listener = (KPListener *)context->user_pointer;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  kpserver = g_hash_table_lookup(listener->server_map, GINT_TO_POINTER(context->socket_id));
  if(kpserver)
    {
      guchar *gnodeid = (guchar *)g_strndup( (gchar *) nodeid, (gint) nodeid_len);
      guchar *gspaceid = (guchar *)g_strndup( (gchar *) spaceid, (gint) spaceid_len);
      guchar *gquery = (guchar *)g_strndup( (gchar *) query, (gint) query_len);
      QueryType type;
      switch( query_format)
	{
	case QFORMAT_RDF_M3:
	  type = QueryTypeTemplate;
	  break;
	case QFORMAT_WQL_VALUES:
	  type = QueryTypeWQLValues;
	  break;
	case QFORMAT_WQL_NODETYPES:
	  type = QueryTypeWQLNodeTypes;
	  break;
	case QFORMAT_WQL_RELATED:
	  type = QueryTypeWQLRelated;
	  break;
	case QFORMAT_WQL_ISTYPE:
	  type = QueryTypeWQLIsType;
	  break;
	case QFORMAT_WQL_ISSUBTYPE:
	  type = QueryTypeWQLIsSubType;
	  break;
	case QFORMAT_SPARQL:
	  type = QueryTypeTemplate;
	  break;
	default:
	  type = -1;
	  break;
	}
       whiteboard_log_debug("SubsceribeReq\n");
       whiteboard_log_debug("\tspaceid: %s\n",gspaceid);
       whiteboard_log_debug("\tnodeid: %s\n",gnodeid);
       whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
       whiteboard_log_debug("\ttype: %d\n", type);
       whiteboard_log_debug("\tqgraph: %s\n",gquery);
      if(type<0)
	{
	  guchar *results = (guchar *) g_strdup("\n");
	  guchar *subsid =  (guchar *) g_strdup("\n");
	  SibAccessNota_Subscribe_cnf(context,
				      (uint8_t *) gnodeid,
				      (uint16_t) strlen( (gchar *)gnodeid),
				      (uint8_t *) gspaceid,
				      (uint16_t) strlen( (gchar *)gspaceid),
				      (uint16_t) msgnum,
				      SSStatus_KP_Request,
				      (uint8_t *) subsid,
				      (uint16_t) strlen( (gchar *)subsid),
				      (uint8_t *) results,
				      (uint16_t) strlen( (gchar *)results));
	  g_free(results);
	  g_free(subsid);
	}
      else
	{
	  kp_server_send_subscribe(kpserver, gspaceid, gnodeid, (gint) msgnum, type, gquery);
	}
      g_free(gnodeid);
      g_free(gspaceid);
      g_free(gquery);
    }
  else
    {
      whiteboard_log_warning("KPServer w/ fd :%d not found\n", context->socket_id);
    }
  whiteboard_log_debug_fe();
}

void SibAccessNota_Unsubscribe_req_process( struct context_pointer* context, uint8_t* nodeid, uint16_t nodeid_len, uint8_t* spaceid, uint16_t spaceid_len, uint32_t msgnum, uint8_t* subscription_id, uint16_t subscription_id_len )
{
  KPListener *listener = (KPListener *)context->user_pointer;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  kpserver = g_hash_table_lookup(listener->server_map, GINT_TO_POINTER(context->socket_id));
  if(kpserver)
    {
      guchar *gnodeid = (guchar *)g_strndup( (gchar *) nodeid, (gint) nodeid_len);
      guchar *gspaceid = (guchar *)g_strndup( (gchar *) spaceid, (gint) spaceid_len);
      guchar *gsubscription_id = (guchar *)g_strndup( (gchar *) subscription_id, (gint) subscription_id_len);
      whiteboard_log_debug("UnsubscribeReq\n");
       whiteboard_log_debug("\tspaceid: %s\n",gspaceid);
       whiteboard_log_debug("\tnodeid: %s\n",gnodeid);
       whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
       whiteboard_log_debug("\tsubid: %s\n",gsubscription_id);

      kp_server_send_unsubscribe(kpserver, gspaceid, gnodeid, (gint) msgnum, gsubscription_id);
  
      g_free(gnodeid);
      g_free(gspaceid);
      g_free(gsubscription_id);
    }
  else
    {
      whiteboard_log_warning("KPServer w/ fd :%d not found\n", context->socket_id);
    }
  whiteboard_log_debug_fe();
}

void SibAccessNota_Leave_req_process( struct context_pointer* context, uint8_t* nodeid, uint16_t nodeid_len, uint8_t* spaceid, uint16_t spaceid_len, uint32_t msgnum )
{
  KPListener *listener = (KPListener *)context->user_pointer;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  kpserver = g_hash_table_lookup(listener->server_map, GINT_TO_POINTER(context->socket_id));
  if(kpserver)
    {
      guchar *gnodeid = (guchar *)g_strndup( (gchar *) nodeid, (gint) nodeid_len);
      guchar *gspaceid = (guchar *)g_strndup( (gchar *) spaceid, (gint) spaceid_len);
       whiteboard_log_debug("LeaveReq\n");
       whiteboard_log_debug("\tspaceid: %s\n",gspaceid);
       whiteboard_log_debug("\tnodeid: %s\n",gnodeid);
       whiteboard_log_debug("\tmsgnum: %d\n", msgnum);

      kp_server_send_leave(kpserver, gspaceid, gnodeid, (gint) msgnum);
      
      g_free(gnodeid);
      g_free(gspaceid);
    }
  else
    {
      whiteboard_log_warning("KPServer w/ fd :%d not found\n", context->socket_id);
    }

  whiteboard_log_debug_fe();
}

/* these hooks must be implemented by user. */
/* This function is called when connection was closed */
void SibAccessNota_service_handler_disconnected(struct context_pointer* context)
{
  KPListener *listener = (KPListener *)context->user_pointer;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  kpserver = g_hash_table_lookup(listener->server_map, GINT_TO_POINTER(context->socket_id));
  if(kpserver)
    {
      listener->error_sockets = g_slist_prepend(listener->error_sockets, GINT_TO_POINTER(context->socket_id));
    }
  else
    {
      whiteboard_log_warning("KPServer w/ fd :%d not found\n", context->socket_id);
    }
  whiteboard_log_debug_fe();
}

/* This function is called when there was out-of-memory or other not so critical error (connection is still usable) */
void SibAccessNota_service_handler_error(struct context_pointer* context, int error_type)
{
  KPListener *listener = (KPListener *)context->user_pointer;
  KPServer *kpserver = NULL;
  whiteboard_log_debug_fb();
  kpserver = g_hash_table_lookup(listener->server_map, GINT_TO_POINTER(context->socket_id));
  if(kpserver)
    {
      listener->error_sockets = g_slist_prepend(listener->error_sockets, GINT_TO_POINTER(context->socket_id));
    }
  else
    {
      whiteboard_log_warning("KPServer w/ fd :%d not found\n", context->socket_id);
    }
  whiteboard_log_debug_fe();
}
