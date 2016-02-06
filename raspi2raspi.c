//-------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Copyright (c) 2016 Andrew Duncan
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <bsd/libutil.h>

#include <sys/mman.h>
#include <sys/time.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "bcm_host.h"
#pragma GCC diagnostic pop

#include "syslogUtilities.h"

//-------------------------------------------------------------------------

#ifndef ALIGN_TO_16
#define ALIGN_TO_16(x) ((x + 15) & ~15)
#endif

#define DEFAULT_SOURCE_DISPLAY_NUMBER 0
#define DEFAULT_DESTINATION_DISPLAY_NUMBER 5
#define DEFAULT_FPS 10

//-------------------------------------------------------------------------

volatile bool run = true;

//-------------------------------------------------------------------------

void
printUsage(
    FILE *fp,
    const char *name)
{
    fprintf(fp, "\n");
    fprintf(fp, "Usage: %s <options>\n", name);
    fprintf(fp, "\n");
    fprintf(fp, "    --daemon - start in the background as a daemon\n");
    fprintf(fp, "    --source <number> - Raspberry Pi display number");
    fprintf(fp, " (default %d)\n", DEFAULT_SOURCE_DISPLAY_NUMBER);
    fprintf(fp, "    --destination <number> - Raspberry Pi display number");
    fprintf(fp, " (default %d)\n", DEFAULT_DESTINATION_DISPLAY_NUMBER);
    fprintf(fp, "    --fps <fps> - set desired frames per second");
    fprintf(fp, " (default %d frames per second)\n", DEFAULT_FPS);
    fprintf(fp, "    --pidfile <pidfile> - create and lock PID file");
    fprintf(fp, " (if being run as a daemon)\n");
    fprintf(fp, "    --help - print usage and exit\n");
    fprintf(fp, "\n");
}

//-------------------------------------------------------------------------

static void
signalHandler(
    int signalNumber)
{
    switch (signalNumber)
    {
    case SIGINT:
    case SIGTERM:

        run = false;
        break;
    };
}

//-------------------------------------------------------------------------

int
main(
    int argc,
    char *argv[])
{
    const char *program = basename(argv[0]);

    int fps = DEFAULT_FPS;
    suseconds_t frameDuration =  1000000 / fps;
    bool isDaemon =  false;
    uint32_t sourceDisplayNumber = DEFAULT_SOURCE_DISPLAY_NUMBER;
    uint32_t destDisplayNumber = DEFAULT_DESTINATION_DISPLAY_NUMBER;
    const char *pidfile = NULL;

    //---------------------------------------------------------------------

    static const char *sopts = "d:f:hp:s:D";
    static struct option lopts[] = 
    {
        { "destination", required_argument, NULL, 'd' },
        { "fps", required_argument, NULL, 'f' },
        { "help", no_argument, NULL, 'h' },
        { "pidfile", required_argument, NULL, 'p' },
        { "source", required_argument, NULL, 's' },
        { "daemon", no_argument, NULL, 'D' },
        { NULL, no_argument, NULL, 0 }
    };

    int opt = 0;

    while ((opt = getopt_long(argc, argv, sopts, lopts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'd':

            destDisplayNumber = atoi(optarg);
            break;

        case 'f':

            fps = atoi(optarg);

            if (fps > 0)
            {
                frameDuration = 1000000 / fps;
            }
            else
            {
                fps = 1000000 / frameDuration;
            }

            break;

        case 'h':

            printUsage(stdout, program);
            exit(EXIT_SUCCESS);

            break;

        case 'p':

            pidfile = optarg;

            break;

        case 's':

            sourceDisplayNumber = atoi(optarg);
            break;

        case 'D':

            isDaemon = true;
            break;

        default:

            printUsage(stderr, program);
            exit(EXIT_FAILURE);

            break;
        }
    }

    //---------------------------------------------------------------------

    struct pidfh *pfh = NULL;

    if (isDaemon)
    {
        if (pidfile != NULL)
        {
            pid_t otherpid;
            pfh = pidfile_open(pidfile, 0600, &otherpid);

            if (pfh == NULL)
            {
                fprintf(stderr,
                        "%s is already running %jd\n",
                        program,
                        (intmax_t)otherpid);
                exit(EXIT_FAILURE);
            }
        }
        
        if (daemon(0, 0) == -1)
        {
            fprintf(stderr, "daemonize failed\n");

            exitAndRemovePidFile(EXIT_FAILURE, pfh);
        }

        if (pfh)
        {
            pidfile_write(pfh);
        }

        openlog(program, LOG_PID, LOG_USER);
    }

    //---------------------------------------------------------------------

    if (signal(SIGINT, signalHandler) == SIG_ERR)
    {
        perrorLog(isDaemon, program, "installing SIGINT signal handler");

        exitAndRemovePidFile(EXIT_FAILURE, pfh);
    }

    //---------------------------------------------------------------------

    if (signal(SIGTERM, signalHandler) == SIG_ERR)
    {
        perrorLog(isDaemon, program, "installing SIGTERM signal handler");

        exitAndRemovePidFile(EXIT_FAILURE, pfh);
    }

    //---------------------------------------------------------------------

    bcm_host_init();

    //---------------------------------------------------------------------

    int result = 0;

    //---------------------------------------------------------------------

    DISPMANX_DISPLAY_HANDLE_T sourceDisplay
        = vc_dispmanx_display_open(sourceDisplayNumber);

    if (sourceDisplay == 0)
    {
        messageLog(isDaemon,
                   program,
                   LOG_ERR,
                   "open source display failed");
        exitAndRemovePidFile(EXIT_FAILURE, pfh);
    }

    DISPMANX_MODEINFO_T sourceInfo;

    result = vc_dispmanx_display_get_info(sourceDisplay, &sourceInfo);

    if (result != 0)
    {
        messageLog(isDaemon,
                   program,
                   LOG_ERR,
                   "getting source display dimensions failed");

        exitAndRemovePidFile(EXIT_FAILURE, pfh);
    }

    //---------------------------------------------------------------------

    DISPMANX_DISPLAY_HANDLE_T destDisplay
        = vc_dispmanx_display_open(destDisplayNumber);

    if (destDisplay == 0)
    {
        messageLog(isDaemon,
                   program,
                   LOG_ERR,
                   "open destination display failed");
        exitAndRemovePidFile(EXIT_FAILURE, pfh);
    }

    DISPMANX_MODEINFO_T destInfo;

    result = vc_dispmanx_display_get_info(destDisplay, &destInfo);

    if (result != 0)
    {
        messageLog(isDaemon,
                   program,
                   LOG_ERR,
                   "getting destination display dimensions failed");
        exitAndRemovePidFile(EXIT_FAILURE, pfh);
    }

    //---------------------------------------------------------------------

    messageLog(isDaemon,
               program,
               LOG_INFO,
               "copying from [%d] %dx%d to [%d] %dx%d",
               sourceDisplayNumber,
               sourceInfo.width,
               sourceInfo.height,
               destDisplayNumber,
               destInfo.width,
               destInfo.height);

    //---------------------------------------------------------------------

	VC_IMAGE_TYPE_T imageType = VC_IMAGE_RGBA32;
	int32_t bytesPerPixel = 4;

    int32_t pitch = bytesPerPixel * ALIGN_TO_16(destInfo.width);
    int32_t length = pitch * destInfo.height;

    void *image = malloc(length);

    if (image == NULL)
    {
        messageLog(isDaemon,
                   program,
                   LOG_ERR,
                   "unable to allocate image buffer");
        exitAndRemovePidFile(EXIT_FAILURE, pfh);
    }

    //---------------------------------------------------------------------

    uint32_t image_ptr;

    DISPMANX_RESOURCE_HANDLE_T sourceResource = 
        vc_dispmanx_resource_create(imageType,
                                    destInfo.width,
                                    destInfo.height,
                                    &image_ptr);

    DISPMANX_RESOURCE_HANDLE_T destResource = 
        vc_dispmanx_resource_create(imageType,
                                    destInfo.width,
                                    destInfo.height,
                                    &image_ptr);

    //---------------------------------------------------------------------

    VC_RECT_T sourceRect;
    vc_dispmanx_rect_set(&sourceRect,
                         0,
                         0,
                         destInfo.width << 16,
                         destInfo.height << 16);
    
    VC_RECT_T destRect;
    vc_dispmanx_rect_set(&destRect, 0, 0, 0, 0);

    VC_RECT_T bmpRect;
    vc_dispmanx_rect_set(&bmpRect,
                         0,
                         0,
                         destInfo.width,
                         destInfo.height);

    //---------------------------------------------------------------------

    VC_DISPMANX_ALPHA_T alpha =
    {
        DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS,
        255,
        0
    };

    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);

    if (update == 0)
    {
        messageLog(isDaemon,
                   program,
                   LOG_ERR,
                   "display update failed");
        exitAndRemovePidFile(EXIT_FAILURE, pfh);
    }

    DISPMANX_ELEMENT_HANDLE_T element;
    element = vc_dispmanx_element_add(update,
                                      destDisplay,
                                      10,
                                      &destRect,
                                      destResource,
                                      &sourceRect,
                                      DISPMANX_PROTECTION_NONE,
                                      &alpha,
                                      NULL,
                                      DISPMANX_NO_ROTATE);
    if (element == 0)
    {
        messageLog(isDaemon,
                   program,
                   LOG_ERR,
                   "failed to create DispmanX element");
        exitAndRemovePidFile(EXIT_FAILURE, pfh);
    }

    vc_dispmanx_update_submit_sync(update);

    //---------------------------------------------------------------------

    struct timeval start_time;
    struct timeval end_time;
    struct timeval elapsed_time;

    //---------------------------------------------------------------------

    while (run)
    {
        gettimeofday(&start_time, NULL);

        //-----------------------------------------------------------------

        result = vc_dispmanx_snapshot(sourceDisplay,
                                      sourceResource,
                                      DISPMANX_NO_ROTATE);

        if (result != 0)
        {
            messageLog(isDaemon,
                       program,
                       LOG_ERR,
                       "DispmanX snapshot failed");
            exitAndRemovePidFile(EXIT_FAILURE, pfh);
        }

        result = vc_dispmanx_resource_read_data(sourceResource,
                                                &bmpRect,
                                                image,
                                                pitch);

        if (result != 0)
        {
            messageLog(isDaemon,
                       program,
                       LOG_ERR,
                       "DispmanX read data failed");
            exitAndRemovePidFile(EXIT_FAILURE, pfh);
        }

        //-----------------------------------------------------------------

        result = vc_dispmanx_resource_write_data(destResource,
                                                 imageType,
                                                 pitch,
                                                 image,
                                                 &bmpRect);

        if (result != 0)
        {
            messageLog(isDaemon,
                       program,
                       LOG_ERR,
                       "DispmanX write data failed");
            exitAndRemovePidFile(EXIT_FAILURE, pfh);
        }

        update = vc_dispmanx_update_start(10);

        if (update == 0)
        {
            messageLog(isDaemon,
                       program,
                       LOG_ERR,
                       "display update failed");
            exitAndRemovePidFile(EXIT_FAILURE, pfh);
        }

        vc_dispmanx_element_change_source(update, element, destResource);
        vc_dispmanx_update_submit_sync(update);

        //-----------------------------------------------------------------

        gettimeofday(&end_time, NULL);
        timersub(&end_time, &start_time, &elapsed_time);

        if (elapsed_time.tv_sec == 0)
        {
            if (elapsed_time.tv_usec < frameDuration)
            {
                usleep(frameDuration -  elapsed_time.tv_usec);
            }
        }
    }

    //---------------------------------------------------------------------

    update = vc_dispmanx_update_start(0);
    vc_dispmanx_element_remove(update, element);
    vc_dispmanx_update_submit_sync(update);

    vc_dispmanx_resource_delete(sourceResource);
    vc_dispmanx_resource_delete(destResource);

    vc_dispmanx_display_close(sourceDisplay);
    vc_dispmanx_display_close(destDisplay);

    free(image);

    //---------------------------------------------------------------------

    messageLog(isDaemon, program, LOG_INFO, "exiting");

    if (isDaemon)
    {
        closelog();
    }

    if (pfh)
    {
        pidfile_remove(pfh);
    }

    //---------------------------------------------------------------------

    return 0 ;
}

