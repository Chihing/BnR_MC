/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  (c) Bernecker + Rainer Industrie-Elektronik Ges.m.b.H.
      B&R Strasse 1, A-5142 Eggelsberg
      www.br-automation.com

  Project:      openPOWERLINK

  Description:  openPOWERLINK process image console demo application

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of the copyright holders nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

****************************************************************************/


/***************************************************************************/
/* includes */
#include <stdio.h>
#include <unistd.h>
#include <pcap.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <string.h>
#include <termios.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <errno.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>

#ifndef CONFIG_POWERLINK_USERSTACK
#include <pthread.h>
#endif

#include "Epl.h"

#include "mc_epl.h"

#include "mc.h"

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------
#define SET_CPU_AFFINITY
#define MAIN_THREAD_PRIORITY            20

#define NODEID      0xF0                //=> MN
#define IP_ADDR     0xc0a86401          // 192.168.100.1
#define SUBNET_MASK 0xFFFFFF00          // 255.255.255.0
#define HOSTNAME    "openPOWERLINK Stack    "
//#define IF_ETH      EPL_VETH_NAME


//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// module global vars
//---------------------------------------------------------------------------

CONST BYTE abMacAddr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint uiNodeId_g = EPL_C_ADR_INVALID;
static uint uiCycleLen_g = 0;
static uint uiCurCycleLen_g = 0;

/* process image stuff */

//typedef unsigned int UINT;

PI_IN AppProcessImageIn_g;
PI_OUT AppProcessImageOut_g;
tEplApiProcessImageCopyJob AppProcessImageCopyJob_g;

#ifdef CONFIG_POWERLINK_USERSTACK

static char* pszCdcFilename_g = "mnobd.cdc";

#else

static pthread_t eventThreadId;
static pthread_t syncThreadId;

void *powerlinkEventThread(void * arg);
void *powerlinkSyncThread(void * arg);

#endif

int nmt_ok = 0;
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

// This function is the entry point for your object dictionary. It is defined
// in OBJDICT.C by define EPL_OBD_INIT_RAM_NAME. Use this function name to define
// this function prototype here. If you want to use more than one Epl
// instances then the function name of each object dictionary has to differ.

tEplKernel PUBLIC  EplObdInitRam (tEplObdInitParam MEM* pInitParam_p);
#if 0
tEplKernel PUBLIC AppCbEvent(
    tEplApiEventType        EventType_p,   // IN: event type (enum)
    tEplApiEventArg*        pEventArg_p,   // IN: event argument (union)
    void GENERIC*           pUserArg_p);
tEplKernel PUBLIC AppCbSync(void);
tEplKernel PUBLIC AppInit(void);
#endif

//---------------------------------------------------------------------------
// Function:            _kbhit
//
// Description:         check if key was pressed
//
// Parameters:          N/A
//
// Returns:             TRUE if key was pressed
//---------------------------------------------------------------------------
static int _kbhit(void)
{
    struct timeval timeout;
    fd_set readFds;
    int maxFd;
    int iSelectRetVal;

    /* initialize file descriptor set */
    maxFd = STDIN_FILENO + 1;
    FD_ZERO(&readFds);
    FD_SET(STDIN_FILENO, &readFds);

    /* initialize timeout value */
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    iSelectRetVal = select(maxFd, &readFds, NULL, NULL, &timeout);
    switch (iSelectRetVal)
    {
        /* select timeout occured, no packet received */
        case 0:
            return FALSE;
            break;

            /* select error occured*/
        case -1:
            return FALSE;
            break;

            /* packet available for receive*/
        default:
            return TRUE;
            break;
    }
}

//---------------------------------------------------------------------------
// Function:            printlog
//
// Description:         print logging entry
//
// Parameters:          fmt             format string
//                      ...             arguments to print
//
// Returns:             N/A
//---------------------------------------------------------------------------
static void printlog(char *fmt, ...)
{
    va_list             arglist;
    time_t              timeStamp;
    struct tm           timeVal;
    char                timeStr[20];

    time(&timeStamp);
    localtime_r(&timeStamp, &timeVal);
    strftime(timeStr, 20, "%Y/%m/%d %H:%M:%S", &timeVal);

    fprintf (stderr, "%s - ", timeStr);
    va_start(arglist, fmt);
    vfprintf(stderr, fmt, arglist);
    va_end(arglist);
}


//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//


//---------------------------------------------------------------------------
//
// Function:    AppCbEvent
//
// Description: event callback function called by EPL API layer within
//              user part (low priority).
//
// Parameters:  EventType_p     = event type
//              pEventArg_p     = pointer to union, which describes
//                                the event in detail
//              pUserArg_p      = user specific argument
//
// Returns:     tEplKernel      = error code,
//                                kEplSuccessful = no error
//                                kEplReject = reject further processing
//                                otherwise = post error event to API layer
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC AppCbEvent(
    tEplApiEventType        EventType_p,   // IN: event type (enum)
    tEplApiEventArg*        pEventArg_p,   // IN: event argument (union)
    void GENERIC*           pUserArg_p __attribute((unused)))

{
    tEplKernel          EplRet = kEplSuccessful;
    UINT                uiVarLen;

    // check if NMT_GS_OFF is reached
    switch (EventType_p)
    {
        case kEplApiEventNmtStateChange:
        {
            switch (pEventArg_p->m_NmtStateChange.m_NewNmtState)
            {
                case kEplNmtGsOff:
                {   // NMT state machine was shut down,
                    // because of user signal (CTRL-C) or critical EPL stack error
                    // -> also shut down EplApiProcess() and main()
                    EplRet = kEplShutdown;

                    printlog("Event:kEplNmtGsOff originating event = 0x%X (%s)\n", pEventArg_p->m_NmtStateChange.m_NmtEvent,
                             EplGetNmtEventStr(pEventArg_p->m_NmtStateChange.m_NmtEvent));
                    break;
                }

                case kEplNmtGsResetCommunication:
                {
                    // continue
                }

                case kEplNmtGsResetConfiguration:
                {
                    if (uiCycleLen_g != 0)
                    {
                        EplRet = EplApiWriteLocalObject(0x1006, 0x00, &uiCycleLen_g, sizeof (uiCycleLen_g));
                        uiCurCycleLen_g = uiCycleLen_g;
                    }
                    else
                    {
                        uiVarLen = sizeof(uiCurCycleLen_g);
                        EplApiReadLocalObject(0x1006, 0x00, &uiCurCycleLen_g, &uiVarLen);
                    }
                    // continue
                }

                case kEplNmtMsPreOperational1:
                {
                    printlog("AppCbEvent(0x%X) originating event = 0x%X (%s)\n",
                           pEventArg_p->m_NmtStateChange.m_NewNmtState,
                           pEventArg_p->m_NmtStateChange.m_NmtEvent,
                           EplGetNmtEventStr(pEventArg_p->m_NmtStateChange.m_NmtEvent));

                    // continue
                }

                case kEplNmtGsInitialising:
                case kEplNmtGsResetApplication:
                case kEplNmtMsNotActive:
                case kEplNmtCsNotActive:
                case kEplNmtCsPreOperational1:
                {
                    break;
                }

                case kEplNmtCsOperational:
                case kEplNmtMsOperational:
                {
                    break;
                }

                default:
                {
                    break;
                }
            }

            break;
        }

        case kEplApiEventCriticalError:
        case kEplApiEventWarning:
        {   // error or warning occured within the stack or the application
            // on error the API layer stops the NMT state machine

            printlog("%s(Err/Warn): Source=%02X EplError=0x%03X",
                                __func__,
                                pEventArg_p->m_InternalError.m_EventSource,
                                pEventArg_p->m_InternalError.m_EplError);
            FTRACE_MARKER("%s(Err/Warn): Source=%02X EplError=0x%03X",
                                __func__,
                                pEventArg_p->m_InternalError.m_EventSource,
                                pEventArg_p->m_InternalError.m_EplError);
            // check additional argument
            switch (pEventArg_p->m_InternalError.m_EventSource)
            {
                case kEplEventSourceEventk:
                case kEplEventSourceEventu:
                {   // error occured within event processing
                    // either in kernel or in user part
                    printlog(" OrgSource=%02X\n", pEventArg_p->m_InternalError.m_Arg.m_EventSource);
                    FTRACE_MARKER(" OrgSource=%02X\n", pEventArg_p->m_InternalError.m_Arg.m_EventSource);
                    break;
                }

                case kEplEventSourceDllk:
                {   // error occured within the data link layer (e.g. interrupt processing)
                    // the DWORD argument contains the DLL state and the NMT event
                    printlog(" val=%X\n", pEventArg_p->m_InternalError.m_Arg.m_dwArg);
                    FTRACE_MARKER(" val=%X\n", pEventArg_p->m_InternalError.m_Arg.m_dwArg);
                    break;
                }

                default:
                {
                    printlog("\n");
                    break;
                }
            }
            break;
        }

        case kEplApiEventHistoryEntry:
        {   // new history entry

            printlog("%s(HistoryEntry): Type=0x%04X Code=0x%04X (0x%02X %02X %02X %02X %02X %02X %02X %02X)\n",
                    __func__,
                    pEventArg_p->m_ErrHistoryEntry.m_wEntryType,
                    pEventArg_p->m_ErrHistoryEntry.m_wErrorCode,
                    (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[0],
                    (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[1],
                    (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[2],
                    (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[3],
                    (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[4],
                    (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[5],
                    (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[6],
                    (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[7]);
            FTRACE_MARKER("%s(HistoryEntry): Type=0x%04X Code=0x%04X (0x%02X %02X %02X %02X %02X %02X %02X %02X)\n",
                                __func__,
                                pEventArg_p->m_ErrHistoryEntry.m_wEntryType,
                                pEventArg_p->m_ErrHistoryEntry.m_wErrorCode,
                                (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[0],
                                (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[1],
                                (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[2],
                                (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[3],
                                (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[4],
                                (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[5],
                                (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[6],
                                (WORD) pEventArg_p->m_ErrHistoryEntry.m_abAddInfo[7]);
            break;
        }

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
        case kEplApiEventNode:
        {
            // check additional argument
            switch (pEventArg_p->m_Node.m_NodeEvent)
            {
                case kEplNmtNodeEventCheckConf:
                {
                    printlog("%s(Node=0x%X, CheckConf)\n", __func__, pEventArg_p->m_Node.m_uiNodeId);
                    break;
                }

                case kEplNmtNodeEventUpdateConf:
                {
                    printlog("%s(Node=0x%X, UpdateConf)\n", __func__, pEventArg_p->m_Node.m_uiNodeId);
                    break;
                }

                case kEplNmtNodeEventNmtState:
                {
                    printlog("%s(Node=0x%X, NmtState=%s)\n", __func__, pEventArg_p->m_Node.m_uiNodeId, EplGetNmtStateStr(pEventArg_p->m_Node.m_NmtState));
                    if(pEventArg_p->m_Node.m_uiNodeId == 2
                    		&& pEventArg_p->m_Node.m_NmtState == kEplNmtCsOperational)
                    {
                    	nmt_ok = 1;
                    }
                    break;
                }

                case kEplNmtNodeEventError:
                {
                    printlog("%s(Node=0x%X, Error=0x%X)\n", __func__, pEventArg_p->m_Node.m_uiNodeId, pEventArg_p->m_Node.m_wErrorCode);

                    break;
                }

                case kEplNmtNodeEventFound:
                {
                    printlog("%s(Node=0x%X, Found)\n", __func__, pEventArg_p->m_Node.m_uiNodeId);

                    break;
                }

                default:
                {
                    break;
                }
            }
            break;
        }

#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_CFM)) != 0)
       case kEplApiEventCfmProgress:
        {
        #if 1
            printlog("%s(Node=0x%X, CFM-Progress: Object 0x%X/%u, ", __func__, pEventArg_p->m_CfmProgress.m_uiNodeId, pEventArg_p->m_CfmProgress.m_uiObjectIndex, pEventArg_p->m_CfmProgress.m_uiObjectSubIndex);
            printlog("%lu/%lu Bytes", (ULONG) pEventArg_p->m_CfmProgress.m_dwBytesDownloaded, (ULONG) pEventArg_p->m_CfmProgress.m_dwTotalNumberOfBytes);
            if ((pEventArg_p->m_CfmProgress.m_dwSdoAbortCode != 0)
                || (pEventArg_p->m_CfmProgress.m_EplError != kEplSuccessful))
            {
                printlog(" -> SDO Abort=0x%lX, Error=0x%X)\n", (unsigned long) pEventArg_p->m_CfmProgress.m_dwSdoAbortCode,
                                                              pEventArg_p->m_CfmProgress.m_EplError);
            }
            else
            {
                printlog(")\n");
            }
            #endif
            break;
        }

        case kEplApiEventCfmResult:
        {
            switch (pEventArg_p->m_CfmResult.m_NodeCommand)
            {
                case kEplNmtNodeCommandConfOk:
                {
                    printlog("%s(Node=0x%X, ConfOk)\n", __func__, pEventArg_p->m_CfmResult.m_uiNodeId);
                    break;
                }

                case kEplNmtNodeCommandConfErr:
                {
                    printlog("%s(Node=0x%X, ConfErr)\n", __func__, pEventArg_p->m_CfmResult.m_uiNodeId);
                    break;
                }

                case kEplNmtNodeCommandConfReset:
                {
                    printlog("%s(Node=0x%X, ConfReset)\n", __func__, pEventArg_p->m_CfmResult.m_uiNodeId);
                    break;
                }

                case kEplNmtNodeCommandConfRestored:
                {
                    printlog("%s(Node=0x%X, ConfRestored)\n", __func__, pEventArg_p->m_CfmResult.m_uiNodeId);
                    break;
                }

                default:
                {
                    printlog("%s(Node=0x%X, CfmResult=0x%X)\n", __func__, pEventArg_p->m_CfmResult.m_uiNodeId, pEventArg_p->m_CfmResult.m_NodeCommand);
                    break;
                }
            }
            break;
        }
#endif

        default:
            break;
    }

    return EplRet;
}

#ifndef CONFIG_POWERLINK_USERSTACK

void *powerlinkEventThread(void * arg __attribute__((unused)))
{
    EplApiProcess();

    return NULL;
}

void *powerlinkSyncThread(void * arg __attribute__((unused)))
{
    while (1)
    {
        AppCbSync();
    }
    return NULL;
}

#endif


//---------------------------------------------------------------------------
//
// Function:            epl_init
//
// Description:         main function of demo application
//
// Parameters:
//
// Returns:
//---------------------------------------------------------------------------

int epl_init(char* devName)
{
    tEplKernel                  EplRet = kEplSuccessful;
    static tEplApiInitParam     EplApiInitParam;
    char*                       sHostname = HOSTNAME;

#ifdef CONFIG_POWERLINK_USERSTACK
    struct sched_param          schedParam;

#endif


#ifdef CONFIG_POWERLINK_USERSTACK
    /* adjust process priority */
    if (nice (-20) == -1)         // push nice level in case we have no RTPreempt
    {
        EPL_DBGLVL_ERROR_TRACE2("%s() couldn't set nice value! (%s)\n", __func__, strerror(errno));
    }
    schedParam.__sched_priority = MAIN_THREAD_PRIORITY;
    if (pthread_setschedparam(pthread_self(), SCHED_RR, &schedParam) != 0)
    {
        EPL_DBGLVL_ERROR_TRACE2("%s() couldn't set thread scheduling parameters! %d\n",
                __func__, schedParam.__sched_priority);
    }

#ifdef SET_CPU_AFFINITY
    {
        /* binds all openPOWERLINK threads to the first CPU core */
        cpu_set_t                   affinity;

        CPU_ZERO(&affinity);
        CPU_SET(0, &affinity);
        sched_setaffinity(0, sizeof(cpu_set_t), &affinity);
    }
#endif

    /* Initialize target specific stuff */
    EplTgtInit();

#endif

    /* Enabling ftrace for debugging */
    FTRACE_OPEN();
    FTRACE_ENABLE(TRUE);

    /*
    EPL_DBGLVL_ALWAYS_TRACE2("%s(): Main Thread Id:%ld\n", __func__,
                             syscall(SYS_gettid));
                             */
    printf("----------------------------------------------------\n");
    printf("openPOWERLINK console process image DEMO application\n");
    printf("----------------------------------------------------\n");

    EPL_MEMSET(&EplApiInitParam, 0, sizeof (EplApiInitParam));
    EplApiInitParam.m_uiSizeOfStruct = sizeof (EplApiInitParam);

#ifdef CONFIG_POWERLINK_USERSTACK

    EplApiInitParam.m_HwParam.m_pszDevName = devName;
    printf("devName is %s\n", devName);
#endif

    EplApiInitParam.m_uiNodeId = uiNodeId_g = NODEID;
    EplApiInitParam.m_dwIpAddress = (0xFFFFFF00 & IP_ADDR) | EplApiInitParam.m_uiNodeId;

    /* write 00:00:00:00:00:00 to MAC address, so that the driver uses the real hardware address */
    EPL_MEMCPY(EplApiInitParam.m_abMacAddress, abMacAddr, sizeof (EplApiInitParam.m_abMacAddress));

    EplApiInitParam.m_fAsyncOnly = FALSE;

    EplApiInitParam.m_dwFeatureFlags = -1;
    EplApiInitParam.m_dwCycleLen = uiCycleLen_g;        // required for error detection
    EplApiInitParam.m_uiIsochrTxMaxPayload = 256;       // const
    EplApiInitParam.m_uiIsochrRxMaxPayload = 256;       // const
    EplApiInitParam.m_dwPresMaxLatency = 50000;         // const; only required for IdentRes
    EplApiInitParam.m_uiPreqActPayloadLimit = 36;       // required for initialisation (+28 bytes)
    EplApiInitParam.m_uiPresActPayloadLimit = 36;       // required for initialisation of Pres frame (+28 bytes)
    EplApiInitParam.m_dwAsndMaxLatency = 150000;        // const; only required for IdentRes
    EplApiInitParam.m_uiMultiplCycleCnt = 0;            // required for error detection
    EplApiInitParam.m_uiAsyncMtu = 1500;                // required to set up max frame size
    EplApiInitParam.m_uiPrescaler = 2;                  // required for sync
    EplApiInitParam.m_dwLossOfFrameTolerance = 500000;
    EplApiInitParam.m_dwAsyncSlotTimeout = 3000000;
    EplApiInitParam.m_dwWaitSocPreq = 150000;
    EplApiInitParam.m_dwDeviceType = -1;      // NMT_DeviceType_U32
    EplApiInitParam.m_dwVendorId = -1;        // NMT_IdentityObject_REC.VendorId_U32
    EplApiInitParam.m_dwProductCode = -1;     // NMT_IdentityObject_REC.ProductCode_U32
    EplApiInitParam.m_dwRevisionNumber = -1;  // NMT_IdentityObject_REC.RevisionNo_U32
    EplApiInitParam.m_dwSerialNumber = -1;    // NMT_IdentityObject_REC.SerialNo_U32

    EplApiInitParam.m_dwSubnetMask = SUBNET_MASK;
    EplApiInitParam.m_dwDefaultGateway = 0;
    EPL_MEMCPY(EplApiInitParam.m_sHostname, sHostname, sizeof(EplApiInitParam.m_sHostname));
    EplApiInitParam.m_uiSyncNodeId = EPL_C_ADR_SYNC_ON_SOA;
    EplApiInitParam.m_fSyncOnPrcNode = FALSE;

    // set callback functions
    EplApiInitParam.m_pfnCbEvent = AppCbEvent;

#ifdef CONFIG_POWERLINK_USERSTACK
    EplApiInitParam.m_pfnObdInitRam = EplObdInitRam;
    EplApiInitParam.m_pfnCbSync  = AppCbSync;
#else
    EplApiInitParam.m_pfnCbSync = NULL;
#endif


    printf("\n\nHello, I'm a Linux Userspace POWERLINK node running as %s!\n  (build: %s / %s)\n\n",
            (uiNodeId_g == EPL_C_ADR_MN_DEF_NODE_ID ?
                "Managing Node" : "Controlled Node"),
            __DATE__, __TIME__);

    // initialize POWERLINK stack
    printf ("Initializing openPOWERLINK stack...\n");
    EplRet = EplApiInitialize(&EplApiInitParam);
    if(EplRet != kEplSuccessful)
    {
        printf("EplApiInitialize() failed (Error:0x%x!\n", EplRet);
        goto Exit;
    }

    // initialize application
    printf ("Initializing openPOWERLINK application...\n");
    EplRet = AppInit();
    if(EplRet != kEplSuccessful)
    {
        printf("ApiInit() failed!\n");
        goto Exit;
    }


#ifdef CONFIG_POWERLINK_USERSTACK
    /* At this point, we don't need any more the device list. Free it */
    //pcap_freealldevs(alldevs);

    EplRet = EplApiSetCdcFilename(pszCdcFilename_g);
    if(EplRet != kEplSuccessful)
    {
        goto Exit;
    }
#else
    // create event thread
    if (pthread_create(&eventThreadId, NULL,
                   &powerlinkEventThread, NULL) != 0)
    {
        goto Exit;
    }

    // create sync thread
    if (pthread_create(&syncThreadId, NULL,
                   &powerlinkSyncThread, NULL) != 0)
    {
        goto Exit;
    }
#endif

    printf("Initializing process image...\n");
    printf("Size of input process image: %ld\n", sizeof(AppProcessImageIn_g));
    printf("Size of output process image: %ld\n", sizeof (AppProcessImageOut_g));
    AppProcessImageCopyJob_g.m_fNonBlocking = FALSE;
    AppProcessImageCopyJob_g.m_uiPriority = 0;
    AppProcessImageCopyJob_g.m_In.m_pPart = &AppProcessImageIn_g;
    AppProcessImageCopyJob_g.m_In.m_uiOffset = 0;
    AppProcessImageCopyJob_g.m_In.m_uiSize = sizeof (AppProcessImageIn_g);
    AppProcessImageCopyJob_g.m_Out.m_pPart = &AppProcessImageOut_g;
    AppProcessImageCopyJob_g.m_Out.m_uiOffset = 0;
    AppProcessImageCopyJob_g.m_Out.m_uiSize = sizeof (AppProcessImageOut_g);

    EplRet = EplApiProcessImageAlloc(sizeof (AppProcessImageIn_g), sizeof (AppProcessImageOut_g), 2, 2);
    if (EplRet != kEplSuccessful)
    {
        goto Exit;
    }

    EplRet = EplApiProcessImageSetup();
    if (EplRet != kEplSuccessful)
    {
        goto Exit;
    }

    // start processing
    EplRet = EplApiExecNmtCommand(kEplNmtEventSwReset);
    if (EplRet != kEplSuccessful)
    {
        goto ExitShutdown;
    }
    else
    {
        return EplRet;
    }
    FTRACE_ENABLE(FALSE);

ExitShutdown:
    // halt the NMT state machine
    // so the processing of POWERLINK frames stops
    EplRet = EplApiExecNmtCommand(kEplNmtEventSwitchOff);
    // delete instance for all modules
    EplRet = EplApiShutdown();

Exit:
    return EplRet;
}


//---------------------------------------------------------------------------
//
// Function:            main
//
// Description:         main function of demo application
//
// Parameters:
//
// Returns:
//---------------------------------------------------------------------------


int epl_reset()
{
    tEplKernel                  EplRet = kEplSuccessful;

    EplRet = EplApiExecNmtCommand(kEplNmtEventSwReset);
    if (EplRet != kEplSuccessful)
    {
        // halt the NMT state machine
        // so the processing of POWERLINK frames stops
        EplRet = EplApiExecNmtCommand(kEplNmtEventSwitchOff);
        // delete instance for all modules
        EplRet = EplApiShutdown();        
    }
    
    return EplRet;

}

