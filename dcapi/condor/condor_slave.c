/*
 * condor/condor_slave.c
 *
 * DC-API functions of slave side
 *
 * (c) Daniel Drotos, 2006
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include "dc_common.h"
#include "dc_client.h"

#include "condor_common.h"
#include "condor_slave.h"
#include "condor_utils.h"


static int _DC_checkpoint_file_requested= 0;
static int _DC_checkpoint_made= 0;

static char *
_DC_cfg(enum _DC_e_param what)
{
	if (what >= cfg_nuof)
		return(NULL);
	if (_DC_params[what].lvalue)
		return(_DC_params[what].lvalue);

	_DC_params[what].lvalue= DC_getCfgStr(_DC_params[what].name);
	if (_DC_params[what].lvalue)
		return(_DC_params[what].lvalue);
	return(_DC_params[what].def);
}

/* Initializes the client API. */
int DC_initClient(void)
{
	char *message;
	extern int _DC_parseCfg(const char *cfgfile);

	int ret= _DC_parseCfg(CLIENT_CONFIG_NAME);
	if (ret)
		fprintf(stderr, "Error parsing configfile %s",
			CLIENT_CONFIG_NAME);

	_DC_init_utils();
	_DC_init_common();

	DC_log(LOG_DEBUG, "Slave dcapi initialized");

	message= _DC_read_message(_DC_cfg(cfg_management_box),
				  _DCAPI_MSG_COMMAND, /*FALSE*/0);
	if (message &&
	    strcmp(message, _DCAPI_CMD_RESUME) == 0)
	{
		DC_log(LOG_INFO, "Resume, restarting...");
		_DC_read_message(_DC_cfg(cfg_management_box),
				 _DCAPI_MSG_COMMAND, /*TRUE*/1);
	}

	return(ret);
}


/* Resolves the local name of input/output files. */
char *DC_resolveFileName(DC_FileType type,
			 const char *logicalFileName)
{
	/* init_log calls this fn, so it is not possible to call DC_log
	   from here */
	/*DC_log(LOG_DEBUG, "DC_resolveFileName(%d,%s)",
	  type, logicalFileName);*/
	char *cn= _DC_cfg(cfg_checkpoint_file);
	if (!strcmp(logicalFileName, DC_CHECKPOINT_FILE))
	{
		switch (type)
		{
		case DC_FILE_IN:
		{
			/* - param of last DC_checkpointMade()
			   - filename created by previous run
			   - NULL otherwise */
			if (_DC_checkpoint_made)
			{
				char *fn= (char*)malloc(strlen(cn)+100);
				sprintf(fn, "%s_finished.txt", cn);
				return(fn);
			}
			else
			{
				FILE *f;
				char *fn= (char*)malloc(strlen(cn)+100);
				sprintf(fn, "%s_finished.txt", cn);
				if ((f= fopen(fn, "r")) != NULL)
				{
					fclose(f);
					return(fn);
				}
				else
				{
					free(fn);
					return(NULL);
				}
			}
			break;
		}
		case DC_FILE_OUT:
		{
			char *s;
			/* new non-existant name */
			if (_DC_checkpoint_file_requested)
			{
				DC_log(LOG_ERR, "Checkpoint file creation "
				       "can not be restarted "
					"(DC_resolveFileName("
					DC_CHECKPOINT_FILE
					") called twice without calling of "
					"DC_checkpointMade())");
				return(NULL);
			}
			_DC_checkpoint_file_requested= 1;
			s= (char*)malloc(strlen(cn)+100);
			sprintf(s, "%s_creating.txt", cn);
			return(s);
			break;
		}
		default:
			return(NULL);
		}
	}
	return(strdup((char*)logicalFileName));
	switch (type)
	{
	case DC_FILE_IN:
	{
		/* registered by DC_addWUInput() */
		break;
	}
	case DC_FILE_OUT:
	{
		/* registered by DC_addWUOutput */
		break;
	}
	case DC_FILE_TMP:
	{
		/* non-registered */
		break;
	}
	default:
		return(NULL);
	}
}


/* Sends a sub-result back to the master. */
int DC_sendResult(const char *logicalFileName,
		  const char *path,
		  DC_FileMode fileMode)
{
	char *fn;
	int ret;
	extern int _DC_copyFile(const char *src, const char *dst);

	DC_log(LOG_DEBUG, "DC_sendResult(%s,%s,%d)",
	       logicalFileName,
	       path,
	       fileMode);
	fn= malloc(strlen(logicalFileName)+100);
	strcpy(fn, _DC_cfg(cfg_subresults_box));
	strcat(fn, "/real_files");
	if ((ret= _DC_mkdir_with_parents(fn, S_IRWXU|
					 S_IRGRP|S_IXGRP|
					 S_IROTH|S_IXOTH)) != DC_OK)
	{
		DC_log(LOG_ERR, "Failed to create dir for subresult (%s): %s",
		       fn, strerror(errno));
		free(fn);
		return(ret);
	}
	strcat(fn, "/");
	strcat(fn, logicalFileName);
	if ((ret= _DC_copyFile(path, fn)) != DC_OK)
	{
		DC_log(LOG_ERR, "Failed to copy subresult file %s to "
		       "%s: %s", path, fn, strerror(errno));
		free(fn);
		return(ret);
	}
	ret= _DC_create_message(_DC_cfg(cfg_subresults_box),
				_DCAPI_MSG_LOGICAL, logicalFileName, NULL);
	free(fn);
	return(ret);
}


/* Sends a message to the master. */
int DC_sendMessage(const char *message)
{
	DC_log(LOG_DEBUG, "DC_sendMessage(%s)", message);
	return _DC_create_message(_DC_cfg(cfg_client_message_box),
				  _DCAPI_MSG_MESSAGE, message, NULL);
}


/* Checks for application control events. */
DC_ClientEvent *DC_checkClientEvent(void)
{
	char *message;
	DC_ClientEvent *e= NULL;

	message= _DC_read_message(_DC_cfg(cfg_master_message_box),
				  _DCAPI_MSG_MESSAGE, /*TRUE*/1);
	if (message)
	{
		if ((e= calloc(1, sizeof(DC_ClientEvent))))
		{
			DC_log(LOG_DEBUG, "API event created: %p", e);
			e->type= DC_CLIENT_MESSAGE;
			e->message= message;
			DC_log(LOG_DEBUG, "Message of the event: %s",
			       e->message);
		}
		else
		{
			free(message);
			DC_log(LOG_ERR, "Failed to create "
			       "API event, memory allocation "
			       "error");
		}
		return(e);
	}

	message= _DC_read_message(_DC_cfg(cfg_management_box),
				  _DCAPI_MSG_COMMAND, /*TRUE*/1);
	if (message &&
	    strcmp(message, _DCAPI_CMD_SUSPEND) == 0)
	{
		char *fn;
		int f;

		DC_log(LOG_INFO, "Master asked me to suspend");
		_DC_create_message(_DC_cfg(cfg_management_box),
				   _DCAPI_MSG_ACK,
				   _DCAPI_ACK_SUSPEND,
				   NULL);

		fn= DC_resolveFileName(DC_FILE_IN, DC_CHECKPOINT_FILE);
		if (fn != NULL)
			free(fn);
		else
		{
			DC_log(LOG_WARNING, "Slave suspending but "
			       "no checkpoint made yet");
			free(fn);
		}

		fflush(NULL);
		fn= DC_resolveFileName(DC_FILE_OUT, DC_LABEL_STDOUT);
		if (fn)
		{
			if ((f= open(fn, O_APPEND)) > 0)
			{
				fsync(f);
				close(f);
			}
			free(fn);
		}
		_DC_create_message(_DC_cfg(cfg_output_cache),
				   DC_LABEL_STDOUT,
				   NULL,
				   DC_LABEL_STDOUT);
		fn= DC_resolveFileName(DC_FILE_OUT, DC_LABEL_STDERR);
		if (fn)
		{
			if ((f= open(fn, O_APPEND)) > 0)
			{
				fsync(f);
				close(f);
			}
			free(fn);
		}
		_DC_create_message(_DC_cfg(cfg_output_cache),
				   DC_LABEL_STDERR,
				   NULL,
				   DC_LABEL_STDERR);

		DC_finishClient(0);
	}
	return(NULL);
}


/* Destroys the event-specific data returned by DC_checkClientEvent(). */
void DC_destroyClientEvent(DC_ClientEvent *event)
{
	DC_log(LOG_DEBUG, "DC_destroyClientEvent(%p)", event);
	if (event)
	{
		switch (event->type)
		{
		case DC_CLIENT_MESSAGE:
		{
			if (event->message)
				free(event->message);
			break;
		}
		default:
			break;
		}
		free(event);
	}
}


/* Indicates that an application-level checkpoint has completed. */
void DC_checkpointMade(const char *fileName)
{
	DC_log(LOG_DEBUG, "DC_checkpointMade(%s)", fileName);
	_DC_checkpoint_file_requested= 0;
	if (fileName)
	{
		char *cn= _DC_cfg(cfg_checkpoint_file);
		char *fn;
		fn= (char*)malloc(strlen(cn)+100);
		sprintf(fn, "%s_finished.txt", cn);
		if (rename(fileName, fn) != 0)
		{
			DC_log(LOG_ERR, "Renaming %s to %s failed: %s",
			       fileName, fn, strerror(errno));
		}
		else
			_DC_checkpoint_made++;
		free(fn);
	}
}


/* Informs the user interface about the fraction of work already done. */
void DC_fractionDone(double fraction)
{
	DC_log(LOG_DEBUG, "DC_fractionDone(%g)", fraction);
}


/* Finishes computation. */
void DC_finishClient(int exitcode)
{
	DC_log(LOG_DEBUG, "DC_finishClient(%d)", exitcode);
	exit(exitcode);
}


/* End of condor/condor_slave.c */

/* Local variables: */
/* c-file-style: "linux" */
/* End: */
