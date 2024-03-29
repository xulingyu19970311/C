/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 */
#include "csapp.h"

// type = 0, not a range request
// type = 1, range request of the form r1-r2
// type = 2, range request of the form r1-
// type = 3, range request of the form -r1
typedef struct rangeNode
{
  int type;
  int first;
  int second;
} rangeNode;

void doit (int fd);
void read_requesthdrs (rio_t * rp, rangeNode * nodePtr);
int parse_uri (char *uri, char *filename, char *cgiargs);
void serve_static (int fd, char *filename, int filesize, rangeNode * nodePtr,
		   int size_flag);
void get_filetype (char *filename, char *filetype);
void serve_dynamic (int fd, char *filename, char *cgiargs);
void clienterror (int fd, char *cause, char *errnum,
		  char *shortmsg, char *longmsg);

int
main (int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
    {
      fprintf (stderr, "usage: %s <port>\n", argv[0]);
      exit (1);
    }

  Signal (SIGPIPE, SIG_IGN);
  listenfd = Open_listenfd (argv[1]);
  while (1)
    {
      clientlen = sizeof (clientaddr);
      connfd = Accept (listenfd, (SA *) & clientaddr, &clientlen);	//line:netp:tiny:accept
      Getnameinfo ((SA *) & clientaddr, clientlen, hostname, MAXLINE,
		   port, MAXLINE, 0);
      printf ("Accepted connection from (%s, %s)\n", hostname, port);
      doit (connfd);		//line:netp:tiny:doit
      Close (connfd);		//line:netp:tiny:close
    }
}

/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void
doit (int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  rangeNode range = { 0, 0, 0 };

  /* Read request line and headers */
  rio_readinitb (&rio, fd);
  if (!rio_readlineb (&rio, buf, MAXLINE))	//line:netp:doit:readrequest
    return;
  printf ("%s", buf);
  sscanf (buf, "%s %s %s", method, uri, version);	//line:netp:doit:parserequest
  if (strcasecmp (method, "GET"))
    {				//line:netp:doit:beginrequesterr
      clienterror (fd, method, "501", "Not Implemented",
		   "Tiny does not implement this method");
      return;
    }				//line:netp:doit:endrequesterr
  read_requesthdrs (&rio, &range);	//line:netp:doit:readrequesthdrs

  /* Parse URI from GET request */
  is_static = parse_uri (uri, filename, cgiargs);
  if (stat (filename, &sbuf) < 0)
    {
      clienterror (fd, filename, "404", "Not found",
		   "Tiny couldn't find this file");
      return;
    }				//line:netp:doit:endnotfound

  if (is_static)
    {				/* Serve static content */
      if (!(S_ISREG (sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
	{			//line:netp:doit:readable
	  clienterror (fd, filename, "403", "Forbidden",
		       "Tiny couldn't read the file");
	  return;
	}
      serve_static (fd, filename, sbuf.st_size, &range, is_static);	//line:netp:doit:servestatic
    }
  else
    {				/* Serve dynamic content */
      if (!(S_ISREG (sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
	{			//line:netp:doit:executable
	  clienterror (fd, filename, "403", "Forbidden",
		       "Tiny couldn't run the CGI program");
	  return;
	}
      serve_dynamic (fd, filename, cgiargs);	//line:netp:doit:servedynamic
    }
}

/* $end doit */

void
process_range (char *buf, rangeNode * nodePtr)
{
  char *next_tok;
  int r1, r2;
  if ((next_tok = strstr (buf, "bytes=")) != NULL)
    {
      next_tok = next_tok + 6;
      if (sscanf (next_tok, "-%u", &r1) == 1)
	{
	  nodePtr->type = 3;
	  nodePtr->first = -r1;
	}
      else if (sscanf (next_tok, "%u-%u", &r1, &r2) == 2)
	{
	  nodePtr->type = 1;
	  nodePtr->first = r1;
	  nodePtr->second = r2;
	}
      else if (sscanf (next_tok, "%u-", &r1) == 1)
	{
	  nodePtr->type = 2;
	  nodePtr->first = r1;
	}
      else
	{
	  nodePtr->type = 0;
	  printf ("get range: error\n");
	}
    }
  printf ("range type: %d, first: %d, second: %d\n", nodePtr->type,
	  nodePtr->first, nodePtr->second);
}

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void
read_requesthdrs (rio_t * rp, rangeNode * nodePtr)
{
  char buf[MAXLINE];

  if (!rio_readlineb (rp, buf, MAXLINE))
    return;
  printf ("%s", buf);
  while (strcmp (buf, "\r\n"))
    {				//line:netp:readhdrs:checkterm
      if (!strncasecmp (buf, "Range:", 6))
	{
	  process_range (buf, nodePtr);
	}
      rio_readlineb (rp, buf, MAXLINE);
      printf ("%s", buf);
    }
  return;
}

/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 *             2 if static but no content-length .nosize
 */
/* $begin parse_uri */
int
parse_uri (char *uri, char *filename, char *cgiargs)
{
  char *ptr;
  int ret_val = 0;
  int len = strlen (uri);

  if (!strstr (uri, "cgi-bin"))
    {
      strcpy (cgiargs, "");
      strcpy (filename, ".");
      if (len > 7 && !strcmp (&uri[len - 7], ".nosize"))
	{
	  strncat (filename, uri, len - 7);
	  ret_val = 2;
	}
      else
	{
	  strcat (filename, uri);
	  ret_val = 1;
	}
      if (uri[strlen (uri) - 1] == '/')
	strcat (filename, "home.html");
      return ret_val;
    }
  else
    {				/* Dynamic content *///line:netp:parseuri:isdynamic
      ptr = index (uri, '?');	//line:netp:parseuri:beginextract
      if (ptr)
	{
	  strcpy (cgiargs, ptr + 1);
	  *ptr = '\0';
	}
      else
	strcpy (cgiargs, "");	//line:netp:parseuri:endextract
      strcpy (filename, ".");	//line:netp:parseuri:beginconvert2
      strcat (filename, uri);	//line:netp:parseuri:endconvert2
      return 0;
    }
}

/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 * size_flag is 1, provide content-length,
 * size_flag is 2, do not provide content-length
 */
/* $begin serve_static */
void
serve_static (int fd, char *filename, int filesize, rangeNode * nodePtr,
	      int size_flag)
{
  int srcfd;
  char *srcp, *srcp1, filetype[MAXLINE], buf[MAXBUF];
  size_t writesize;
  /* Send response headers to client
     First check for types,then see if nodePtr->first<0 or can't satisfy other reqiurement(filter the input) 
     Then we caculate the length of the content depending different types and printout 
      */
  get_filetype (filename, filetype);
  if (nodePtr->type == 1 || 2 || 3)
    {

      if (nodePtr->type == 1)
	{
	  if (nodePtr->first < 0 || nodePtr->second < nodePtr->first)
	    {
	      sprintf (buf, "HTTP/1.1 416 Range Not Satisfiable\r\n");
	      sprintf (buf, "%sServer: Tiny Web Server\r\n", buf);
	      sprintf (buf, "%sConnection: Close\r\n", buf);
	      sprintf (buf, "%sAccept-Ranges: bytes\r\n", buf);
	      sprintf (buf, "%sContent-Rnage: bytes */100\r\n", buf);
	    }
	  else
	    {
	      sprintf (buf, "HTTP/1.1 206 Partial Content\r\n");
	      sprintf (buf, "%sServer: Tiny Web Server\r\n", buf);
	      sprintf (buf, "%sConnection: Close\r\n", buf);
	      sprintf (buf, "%sAccept-Ranges: bytes\r\n", buf);
	      sprintf (buf, "%sContent-Rnage: bytes %d-%d/%d\r\n", buf,
		       nodePtr->first, nodePtr->second, filesize);
	      sprintf (buf, "%sContent-length: %d\r\n", buf,
		       ((nodePtr->second) - (nodePtr->first) + 1));
	      sprintf (buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
	      writesize = strlen (buf);
	      if (rio_writen (fd, buf, strlen (buf)) < writesize)
		{
		  printf ("errors writing to client.\n");
		}
	    }
	}
      if (nodePtr->type == 2)
	{
	  if (nodePtr->first < 0)
	    {
	      sprintf (buf, "HTTP/1.1 416 Range Not Satisfiable\r\n");
	      sprintf (buf, "%sServer: Tiny Web Server\r\n", buf);
	      sprintf (buf, "%sConnection: Close\r\n", buf);
	      sprintf (buf, "%sAccept-Ranges: bytes\r\n", buf);
	      sprintf (buf, "%sContent-Rnage: bytes */100\r\n", buf);
	    }
	  else
	    {
	      sprintf (buf, "HTTP/1.1 206 Partial Content\r\n");
	      sprintf (buf, "%sServer: Tiny Web Server\r\n", buf);
	      sprintf (buf, "%sConnection: Close\r\n", buf);
	      sprintf (buf, "%sAccept-Ranges: bytes\r\n", buf);
	      sprintf (buf, "%sContent-Rnage: bytes %d-%d/%d\r\n", buf,
		       nodePtr->first, filesize - 1, filesize);
	      sprintf (buf, "%sContent-length: %d\r\n", buf,
		       ((100 - (nodePtr->first))));
	      sprintf (buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
	      writesize = strlen (buf);
	      if (rio_writen (fd, buf, strlen (buf)) < writesize)
		{
		  printf ("errors writing to client.\n");
		}
	    }
	}
      if (nodePtr->type == 3)
	{
	  if (nodePtr->first > 0)
	    {
	      sprintf (buf, "HTTP/1.1 416 Range Not Satisfiable\r\n");
	      sprintf (buf, "%sServer: Tiny Web Server\r\n", buf);
	      sprintf (buf, "%sConnection: Close\r\n", buf);
	      sprintf (buf, "%sAccept-Ranges: bytes\r\n", buf);
	      sprintf (buf, "%sContent-Rnage: bytes */100\r\n", buf);
	    }
	  else
	    {
	      sprintf (buf, "HTTP/1.1 206 Partial Content\r\n");
	      sprintf (buf, "%sServer: Tiny Web Server\r\n", buf);
	      sprintf (buf, "%sConnection: Close\r\n", buf);
	      sprintf (buf, "%sAccept-Ranges: bytes\r\n", buf);
	      sprintf (buf, "%sContent-Rnage: bytes %d-%d/%d\r\n", buf,
		       (filesize + nodePtr->first), filesize - 1, filesize);
	      sprintf (buf, "%sContent-length: %d\r\n", buf, -nodePtr->first);
	      sprintf (buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
	    }
	  writesize = strlen (buf);
	  if (rio_writen (fd, buf, strlen (buf)) < writesize)
	    {
	      printf ("errors writing to client.\n");
	    }
	}
    }
  if (nodePtr->type == 0)
    {
      sprintf (buf, "HTTP/1.0 200 OK\r\n");
      sprintf (buf, "%sServer: Tiny Web Server\r\n", buf);
      sprintf (buf, "%sConnection: close\r\n", buf);
      if (size_flag == 1)
	{
	  sprintf (buf, "%sContent-length: %d\r\n", buf, filesize);
	}
      sprintf (buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
      writesize = strlen (buf);
      if (rio_writen (fd, buf, strlen (buf)) < writesize)
	{
	  printf ("errors writing to client.\n");
	}
    }

  printf ("Response headers:\n");
  printf ("%s", buf);
                                    
  /* Send response body to client 
     We set the SRCP which is the beginning point of the memoery according to different situations
     and also update the filelength to new one*/
  srcfd = Open (filename, O_RDONLY, 0);	//line:netp:servestatic:open
  srcp = Mmap (0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  if (nodePtr->type == 0)
    {

      if (rio_writen (fd, srcp, filesize) < filesize)
	{
	  printf ("errors writing to client.\n");
	}
    }
  if (nodePtr->type == 1)
    {
      srcp1 = srcp + (nodePtr->first);
      Close (srcfd);
      if (rio_writen (fd, srcp1, (((nodePtr->second) - (nodePtr->first)) + 1))
	  < (((nodePtr->second) - (nodePtr->first)) + 1))
	{
	  printf ("%d , %d", nodePtr->second, nodePtr->first);
	  printf ("errors writing to client.\n");
	}
    }
  if (nodePtr->type == 2)
    {
      srcp1 = srcp + (nodePtr->first);
      Close (srcfd);
      if (rio_writen (fd, srcp1, (filesize - (nodePtr->first)) + 1) <
	  ((filesize - (nodePtr->first) + 1)))
	{
	  printf ("errors writing to client.\n");
	}
    }
  if (nodePtr->type == 3)
    {
      srcp1 = srcp + ((filesize + (nodePtr->first)));
      Close (srcfd);
      if (rio_writen (fd, srcp1, (-(nodePtr->first))) < (-(nodePtr->first)))
	{
	  printf ("errors writing to client.\n");
	}
    }
  Munmap (srcp, filesize);
}

/*
 * get_filetype - derive file type from file name
 */
void
get_filetype (char *filename, char *filetype)
{
  if (strstr (filename, ".html"))
    strcpy (filetype, "text/html");
  else if (strstr (filename, ".gif"))
    strcpy (filetype, "image/gif");
  else if (strstr (filename, ".png"))
    strcpy (filetype, "image/png");
  else if (strstr (filename, ".jpg"))
    strcpy (filetype, "image/jpeg");
  else if (strstr (filename, ".mp4"))
    strcpy (filetype, "video/mp4");
  else if (strstr (filename, ".mp3"))
    strcpy (filetype, "audio/mp3");
  else
    strcpy (filetype, "text/plain");
}

/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void
serve_dynamic (int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf (buf, "HTTP/1.0 200 OK\r\n");
  rio_writen (fd, buf, strlen (buf));
  sprintf (buf, "Server: Tiny Web Server\r\n");
  rio_writen (fd, buf, strlen (buf));

  if (Fork () == 0)
    {				/* Child *///line:netp:servedynamic:fork
      /* Real server would set all CGI vars here */
      setenv ("QUERY_STRING", cgiargs, 1);	//line:netp:servedynamic:setenv
      Dup2 (fd, STDOUT_FILENO);	/* Redirect stdout to client *///line:netp:servedynamic:dup2
      Execve (filename, emptylist, environ);	/* Run CGI program *///line:netp:servedynamic:execve
    }
  Wait (NULL);			/* Parent waits for and reaps child *///line:netp:servedynamic:wait
}

/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void
clienterror (int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf (body, "<html><title>Tiny Error</title>");
  sprintf (body, "%s<body bgcolor=" "ffffff" ">\r\n", body);
  sprintf (body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf (body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf (body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf (buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  sprintf (buf, "%sContent-type: text/html\r\n", buf);
  sprintf (buf, "%sContent-length: %d\r\n\r\n", buf, (int) strlen (body));
  rio_writen (fd, buf, strlen (buf));
  rio_writen (fd, body, strlen (body));
}

/* $end clienterror */
