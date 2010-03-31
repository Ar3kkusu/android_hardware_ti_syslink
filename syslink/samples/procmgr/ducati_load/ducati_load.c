/*
 *  Copyright 2001-2008 Texas Instruments - http://www.ti.com/
 * 
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 *  ======== ducati_load.c========
 *  "cexec" is a Linux console-based utility that allows developers to load 
 *  and start a new DSP/BIOS Bridge based DSP program. If "cexec" encounters
 *  an error, it will display a DSP/BIOS Bridge GPP API error code.
 *
 *  Usage:
 *      ducati_load.out [optional args] -p <proc_id> <ducati_program>
 *
 *  Options:
 *      -?: displays "cexec" usage. If this option is set, cexec does not 
 *          load the DSP program.
 *      -w: waits for the user to hit the enter key on the keyboard, which
 *          stops DSP program execution by placing the DSP into reset. This 
 *          will also display on the WinCE console window the contents of
 *          DSP/BIOS Bridge trace buffer, which is used internally by the 
 *          DSP/BIOS Bridge runtime. If this option is not specified, "cexec"
 *          loads and starts the DSP program, then returns immeidately, 
 *          leaving the DSP program running.
 *      -v: verbose mode.
 *      -p [processor]: defines the processor id. 2 for SysM3 and 3 for 
 *        AppM3.
 *      -T: Set scriptable mode. If set, cexec will run to completion after
 *          loading and running a DSP target. User will not be able to stop 
 *          the loaded DSP target.
 *  
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <ProcMgr.h>
#include <Std.h>
#include <OsalPrint.h>
#include <String.h>
#include <MultiProc.h>
#include <omap4430proc.h>
#include <ProcDefs.h>
#include <_ProcMgrDefs.h>
#include "getopt_test.h"

/* global constants. */ 
#define MAXTRACESIZE            256   /* size of trace buffer. */

#define NUM_MEM_ENTRIES         9
#define RESET_VECTOR_ENTRY_ID   0


ProcMgr_Handle ProcMgrApp_handle       = NULL;
/* function prototype. */ 
void DisplayUsage();
void PrintVerbose(char *pstrFmt, ...);

/* global variables. */ 
Bool g_fVerbose = FALSE;

/* 
 *  ======== main ========
 */ 
int main(int argc, char * argv[]) 
{
    int                     opt;
    unsigned int            uProcId = PROC_SYSM3;    /* default proc ID is 0. */
    Bool                    fError = FALSE;
    int                     status = PROCMGR_SUCCESS;
    int                     cArgc = 0;        /* local argc count. */
    Bool                    fScriptable = FALSE;
    extern char *           optarg;
    ProcMgr_AttachParams *  ducati_params;
    ProcMgr_StartParams *   start_params;
    ProcMgr_StopParams      stop_params;
    UInt32                  fileId;
    Char *                  image_path = NULL;
    UInt32                  entry_point;
    ProcMgr_Config *        cfg;
    OMAP4430PROC_Config     procConfig;
    OMAP4430PROC_Params     procParams;
    Handle                  procHandle;
    ProcMgr_Params          params;
    UInt16                  procId;
    UInt32                  optind;
    MultiProc_Config        multiProcConfig;

    Osal_printf ("Entered Ducati load main\n");

#if !defined(SYSLINK_USE_SYSMGR)
    //MultiProc_setLocalId(MultiProc_getId("MPU"));
    multiProcConfig.maxProcessors = 4;
    multiProcConfig.id = 0;
    String_cpy (multiProcConfig.nameList[0], "MPU");
    String_cpy (multiProcConfig.nameList[1], "Tesla");
    String_cpy (multiProcConfig.nameList[2], "SysM3");
    String_cpy (multiProcConfig.nameList[3], "AppM3");
    MultiProc_setLocalId(MultiProc_getId("MPU"));
    MultiProc_setup (&multiProcConfig);
#endif
    optind = 0;

    while ((opt = getopt_test (argc, (char * const)argv, "+lx:p:ZPR:")) != EOF) {
        switch (opt) {
        case 'x':
            /* use given executable */
            image_path = (char *) optarg;
            Osal_printf ("image_path is %s\n",image_path);
            break;

        case 'p':
            /* set procId*/
            uProcId = atoi(optarg);
            Osal_printf ("ProcId is %d\n",uProcId);
            break;

        default:
            fError = TRUE;
            break;
        }
    }

    if (image_path == NULL) {
        fError = TRUE;
    }

    argv += cArgc + 1;
    argc -= cArgc + 1;
    if (fError) {
        DisplayUsage ();
    } else {
        cfg = malloc(sizeof(ProcMgr_Config));
        ducati_params = malloc(sizeof(ProcMgr_AttachParams));
        ProcMgr_getConfig (cfg);
        status = ProcMgr_setup (cfg);
        
        if (status != PROCMGR_SUCCESS) {
            fprintf(stdout, "ProcMgr_setup failed status = 0x%x\n",status);
            exit(1);
        }
        fprintf (stdout, "ProcMgr_setup status: [0x%x]\n", status);
        OMAP4430PROC_getConfig (&procConfig);
        status = OMAP4430PROC_setup (&procConfig);
        if (status < 0) {
            fprintf (stdout, "Error in OMAP4430PROC_setup [0x%x]\n", status);
        }
        /* Get MultiProc ID by name. */
        if (uProcId == PROC_SYSM3) {
            procId = MultiProc_getId ("SysM3");
        } else if (uProcId == PROC_APPM3) {
            procId = MultiProc_getId ("AppM3");
        } else {
            Osal_printf ("ERROR: Not a Valid ProcId provided %d \n", uProcId);
            DisplayUsage ();
            exit(1);
        }

        /* Create an instance of the Processor object for OMAP3530 */
        OMAP4430PROC_Params_init (NULL, &procParams);
        procParams.numMemEntries       = NUM_MEM_ENTRIES;
        procParams.memEntries          = 0;
        procParams.resetVectorMemEntry = RESET_VECTOR_ENTRY_ID;
        procHandle = OMAP4430PROC_create (procId, &procParams);

        fprintf (stdout, "MultiProc_getId procId: [0x%x]\n", procId);
        /* Initialize parameters */
        ProcMgr_Params_init (NULL, &params);
        params.procHandle = procHandle;
        ProcMgrApp_handle = ProcMgr_create (procId, &params);

        if (ProcMgrApp_handle == NULL) {
            fprintf (stdout,"Error in ProcMgr_create \n");
            status = PROCMGR_E_FAIL;
        }

        status = ProcMgr_attach (ProcMgrApp_handle,ducati_params);

        if (status != PROCMGR_SUCCESS) {
            fprintf(stdout,"ProcMgr_attach failed status = 0x%x\n",status);
            exit(1);
        }

        stop_params.proc_id = uProcId;
        status = ProcMgr_stop (ProcMgrApp_handle, &stop_params);
        if (status != PROCMGR_SUCCESS) {
            fprintf(stdout, "ProcMgr_stop failed for status = 0x%x\n", status);
            exit(1);
        }

        Osal_printf("Loading Image %s ..... \n", image_path);
        status  = ProcMgr_load (ProcMgrApp_handle, image_path, argc, &image_path,
                                &entry_point, &fileId, uProcId);


        if (status != PROCMGR_SUCCESS) {
            fprintf(stdout,"ProcMgr_load failed for image %s and "
                        "status = 0x%x\n",image_path, status);
            exit(1);
        }

        Osal_printf ("Completed Loading Image ..... %s\n",image_path);
        fprintf(stdout, "entry_point is 0x%x\n",entry_point);

        start_params = malloc(sizeof(ProcMgr_StartParams));
        if (start_params == NULL) {
            fprintf(stdout,"ProcMgr_load memory allocation failed for "
                        "start_params.\n");
            exit(1);
        }

        start_params->proc_id = uProcId;
        status = ProcMgr_start (ProcMgrApp_handle, entry_point, start_params);
        if (status != PROCMGR_SUCCESS) {
            fprintf(stdout, "ProcMgr_start failed for image %s and "
                    "status = 0x%x\n", image_path, status);
            exit(1);
        }
        status = ProcMgr_detach (ProcMgrApp_handle);
        if (status != PROCMGR_SUCCESS) {
            fprintf(stdout, "ProcMgr_detach failed and status = 0x%x\n", status);
            exit(1);
        }
        
    }

    if (!fScriptable) {
        /* Wait for user to hit any key before exiting. */ 
        fprintf(stdout, "Hit any key to terminate cexec.\n");
        (void)getchar();
    }

    return 0;
}


void DisplayUsage() 
{
    fprintf(stdout, "Usage: cexec -p <ProcId> <Image path>\n");
    fprintf(stdout, "\t[optional arguments]:\n");
    fprintf(stdout, "\t-?: Display cexec usage\n");
    fprintf(stdout, "\t-v: Verbose mode\n");
    fprintf(stdout, "\t-w: Waits for user to hit enter key before\n");
    fprintf(stdout, "\t    terminating. Displays trace buffer\n");
    fprintf(stdout, "\n\t[required arguments]:\n");
    fprintf(stdout, "\t-p [processor]: User-specified processor #\n");
    fprintf(stdout, "\t<dsp program>\n");
    fprintf(stdout, "\n\tExample: cexec -p 2 prog.xem3\n\n");
}


/*
 * ======== PrintVerbose ========
 */ 
void  PrintVerbose(char *pstrFmt,...) 
{
    va_list args;
    if (g_fVerbose) {
        va_start(args, pstrFmt);
        vfprintf(stdout, pstrFmt, args);
        va_end(args);
        fflush(stdout);
    }
}
