/*
 * Copyright (C) 2018  Christian Berger
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"

extern "C" {
    #include <bcm_host.h>
    #include <ilclient.h>
}

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

////////////////////////////////////////////////////////////////////////////////
// Based on: https://github.com/raspberrypi/userland/blob/e5b2684c507442f6a0c7508c34a3f61706dc00e6/host_applications/linux/apps/hello_pi/hello_encode/encode.c#L71-L89

static void
print_def(OMX_PARAM_PORTDEFINITIONTYPE def)
{
   printf("Port %u: %s %u/%u %u %u %s,%s,%s %ux%u %ux%u @%u %u\n",
          def.nPortIndex,
          def.eDir == OMX_DirInput ? "in" : "out",
          def.nBufferCountActual,
          def.nBufferCountMin,
          def.nBufferSize,
          def.nBufferAlignment,
          def.bEnabled ? "enabled" : "disabled",
          def.bPopulated ? "populated" : "not pop.",
          def.bBuffersContiguous ? "contig." : "not cont.",
          def.format.video.nFrameWidth,
          def.format.video.nFrameHeight,
          def.format.video.nStride,
          def.format.video.nSliceHeight,
          def.format.video.xFramerate, def.format.video.eColorFormat);
}

////////////////////////////////////////////////////////////////////////////////

int32_t main(int32_t argc, char **argv) {
    int32_t retCode{1};
    auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
    if ( (0 == commandlineArguments.count("cid")) ||
         (0 == commandlineArguments.count("name")) ||
         (0 == commandlineArguments.count("width")) ||
         (0 == commandlineArguments.count("height")) ) {
        std::cerr << argv[0] << " attaches to an I420-formatted image residing in a shared memory area to convert it into a corresponding h264 frame for publishing to a running OD4 session using OpenMAX." << std::endl;
        std::cerr << "Usage:   " << argv[0] << " --cid=<OpenDaVINCI session> --name=<name of shared memory area> --width=<width> --height=<height> [--gop=<GOP>] [--verbose] [--id=<identifier in case of multiple instances]" << std::endl;
        std::cerr << "         --cid:     CID of the OD4Session to send h264 frames" << std::endl;
        std::cerr << "         --id:      when using several instances, this identifier is used as senderStamp" << std::endl;
        std::cerr << "         --name:    name of the shared memory area to attach" << std::endl;
        std::cerr << "         --width:   width of the frame" << std::endl;
        std::cerr << "         --height:  height of the frame" << std::endl;
        std::cerr << "         --gop:     optional: length of group of pictures (default = 10)" << std::endl;
        std::cerr << "         --verbose: print encoding information" << std::endl;
        std::cerr << "Example: " << argv[0] << " --cid=111 --name=data --width=640 --height=480 --verbose" << std::endl;
    }
    else {
        const std::string NAME{commandlineArguments["name"]};
        const uint32_t WIDTH{static_cast<uint32_t>(std::stoi(commandlineArguments["width"]))};
        const uint32_t HEIGHT{static_cast<uint32_t>(std::stoi(commandlineArguments["height"]))};
        const uint32_t GOP_DEFAULT{10};
        const uint32_t GOP{(commandlineArguments["gop"].size() != 0) ? static_cast<uint32_t>(std::stoi(commandlineArguments["gop"])) : GOP_DEFAULT};
        const bool VERBOSE{commandlineArguments.count("verbose") != 0};
        const uint32_t ID{(commandlineArguments["id"].size() != 0) ? static_cast<uint32_t>(std::stoi(commandlineArguments["id"])) : 0};

        std::unique_ptr<cluon::SharedMemory> sharedMemory(new cluon::SharedMemory{NAME});
        if (sharedMemory && sharedMemory->valid()) {
            std::clog << "[opendlv-video-h264-omx-encoder-rpi]: Attached to '" << sharedMemory->name() << "' (" << sharedMemory->size() << " bytes)." << std::endl;

            bcm_host_init();

            ////////////////////////////////////////////////////////////////////
            // Based on: https://github.com/raspberrypi/userland/blob/e5b2684c507442f6a0c7508c34a3f61706dc00e6/host_applications/linux/apps/hello_pi/hello_encode/encode.c#L94-L236
            OMX_VIDEO_PARAM_PORTFORMATTYPE format;
            OMX_PARAM_PORTDEFINITIONTYPE def;
            COMPONENT_T *video_encode = NULL;
            COMPONENT_T *list[5];
            OMX_BUFFERHEADERTYPE *buf;
            OMX_BUFFERHEADERTYPE *out;
            int r;
            ILCLIENT_T *client;
//            int status = 0;
//            int framenumber = 0;
//            FILE *outf;

            memset(list, 0, sizeof(list));

            if ((client = ilclient_init()) == NULL) {
               return -3;
            }

            if (OMX_Init() != OMX_ErrorNone) {
               ilclient_destroy(client);
               return -4;
            }

            // create video_encode
            r = ilclient_create_component(client, &video_encode, "video_encode",
static_cast<ILCLIENT_CREATE_FLAGS_T>(
                                          ILCLIENT_DISABLE_ALL_PORTS |
                                          ILCLIENT_ENABLE_INPUT_BUFFERS |
                                          ILCLIENT_ENABLE_OUTPUT_BUFFERS
                                    ));
            if (r != 0) {
               printf
                  ("ilclient_create_component() for video_encode failed with %x!\n",
                   r);
               exit(1);
            }
            list[0] = video_encode;

            // get current settings of video_encode component from port 200
            memset(&def, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
            def.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
            def.nVersion.nVersion = OMX_VERSION;
            def.nPortIndex = 200;

            if (OMX_GetParameter
                (ILC_GET_HANDLE(video_encode), OMX_IndexParamPortDefinition,
                 &def) != OMX_ErrorNone) {
               printf("%s:%d: OMX_GetParameter() for video_encode port 200 failed!\n",
                      __FUNCTION__, __LINE__);
               exit(1);
            }

            print_def(def);

            // Port 200: in 1/1 115200 16 enabled,not pop.,not cont. 320x240 320x240 @1966080 20
            def.format.video.nFrameWidth = WIDTH;
            def.format.video.nFrameHeight = HEIGHT;
            def.format.video.xFramerate = 30 << 16;
            def.format.video.nSliceHeight = ALIGN_UP(def.format.video.nFrameHeight, 16);
            def.format.video.nStride = def.format.video.nFrameWidth;
            def.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;

            print_def(def);

            r = OMX_SetParameter(ILC_GET_HANDLE(video_encode),
                                 OMX_IndexParamPortDefinition, &def);
            if (r != OMX_ErrorNone) {
               printf
                  ("%s:%d: OMX_SetParameter() for video_encode port 200 failed with %x!\n",
                   __FUNCTION__, __LINE__, r);
               exit(1);
            }

            memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
            format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
            format.nVersion.nVersion = OMX_VERSION;
            format.nPortIndex = 201;
            format.eCompressionFormat = OMX_VIDEO_CodingAVC;

            printf("OMX_SetParameter for video_encode:201...\n");
            r = OMX_SetParameter(ILC_GET_HANDLE(video_encode),
                                 OMX_IndexParamVideoPortFormat, &format);
            if (r != OMX_ErrorNone) {
               printf
                  ("%s:%d: OMX_SetParameter() for video_encode port 201 failed with %x!\n",
                   __FUNCTION__, __LINE__, r);
               exit(1);
            }

            OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
            // set current bitrate to 1Mbit
            memset(&bitrateType, 0, sizeof(OMX_VIDEO_PARAM_BITRATETYPE));
            bitrateType.nSize = sizeof(OMX_VIDEO_PARAM_BITRATETYPE);
            bitrateType.nVersion.nVersion = OMX_VERSION;
            bitrateType.eControlRate = OMX_Video_ControlRateVariable;
            bitrateType.nTargetBitrate = 1000000;
            bitrateType.nPortIndex = 201;
            r = OMX_SetParameter(ILC_GET_HANDLE(video_encode),
                                OMX_IndexParamVideoBitrate, &bitrateType);
            if (r != OMX_ErrorNone) {
               printf
                 ("%s:%d: OMX_SetParameter() for bitrate for video_encode port 201 failed with %x!\n",
                  __FUNCTION__, __LINE__, r);
               exit(1);
            }


            // get current bitrate
            memset(&bitrateType, 0, sizeof(OMX_VIDEO_PARAM_BITRATETYPE));
            bitrateType.nSize = sizeof(OMX_VIDEO_PARAM_BITRATETYPE);
            bitrateType.nVersion.nVersion = OMX_VERSION;
            bitrateType.nPortIndex = 201;

            if (OMX_GetParameter
                (ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate,
                &bitrateType) != OMX_ErrorNone) {
               printf("%s:%d: OMX_GetParameter() for video_encode for bitrate port 201 failed!\n",
                     __FUNCTION__, __LINE__);
               exit(1);
            }
            printf("Current Bitrate=%u\n",bitrateType.nTargetBitrate);



           printf("encode to idle...\n");
           if (ilclient_change_component_state(video_encode, OMX_StateIdle) == -1) {
              printf
                 ("%s:%d: ilclient_change_component_state(video_encode, OMX_StateIdle) failed",
                  __FUNCTION__, __LINE__);
           }

           printf("enabling port buffers for 200...\n");
           if (ilclient_enable_port_buffers(video_encode, 200, NULL, NULL, NULL) != 0) {
              printf("enabling port buffers for 200 failed!\n");
              exit(1);
           }

           printf("enabling port buffers for 201...\n");
           if (ilclient_enable_port_buffers(video_encode, 201, NULL, NULL, NULL) != 0) {
              printf("enabling port buffers for 201 failed!\n");
              exit(1);
           }

           printf("encode to executing...\n");
           ilclient_change_component_state(video_encode, OMX_StateExecuting);

            ////////////////////////////////////////////////////////////////////
            // Allocate image buffer to hold h264 frame as output.
            std::vector<char> h264Buffer;
            h264Buffer.resize(WIDTH * HEIGHT, '0'); // In practice, this is small than WIDTH * HEIGHT

            // Interface to OD4Session.
            cluon::data::TimeStamp before, after, sampleTimeStamp;

            // Interface to a running OpenDaVINCI session (ignoring any incoming Envelopes).
            cluon::OD4Session od4{static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]))};

            while ( (sharedMemory && sharedMemory->valid()) && od4.isRunning() ) {
                // Wait for incoming frame.
                sharedMemory->wait();

                sampleTimeStamp = cluon::time::now();

                if (VERBOSE) {
                    before = sampleTimeStamp;
                }

                sharedMemory->lock();
                {
                    buf = reinterpret_cast<OMX_BUFFERHEADERTYPE*>(ilclient_get_input_buffer(video_encode, 200, 1));
                    if (NULL != buf) {
                        uint8_t *y = buf->pBuffer;
                        uint8_t *u = y + def.format.video.nStride * def.format.video.nSliceHeight;
                        uint8_t *v = u + (def.format.video.nStride >> 1) * (def.format.video.nSliceHeight >> 1);

                        // Copy data over from shared memory.
                        // Y:
                        memcpy(y, reinterpret_cast<uint8_t*>(sharedMemory->data()), (WIDTH * HEIGHT));
                        // U:
                        memcpy(u, reinterpret_cast<uint8_t*>(sharedMemory->data() + (WIDTH * HEIGHT)), ((WIDTH * HEIGHT) >> 2));
                        // V:
                        memcpy(v, reinterpret_cast<uint8_t*>(sharedMemory->data() + (WIDTH * HEIGHT + ((WIDTH * HEIGHT) >> 2))), ((WIDTH * HEIGHT) >> 2));

                        buf->nFilledLen = (WIDTH * HEIGHT) * 3 / 2;
                    }
                }
                sharedMemory->unlock();

                ssize_t totalSize{0};

                if (NULL != buf) {
                    if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_encode), buf) != OMX_ErrorNone) {
                        printf("Error emptying buffer!\n");
                    }

                    out = ilclient_get_output_buffer(video_encode, 201, 1);

                    if (out != NULL) {
                        totalSize = out->nFilledLen;
                        memcpy(&h264Buffer[0], out->pBuffer,out->nFilledLen);
                        out->nFilledLen = 0;
                    }

                    r = OMX_FillThisBuffer(ILC_GET_HANDLE(video_encode), out);
                    if (r != OMX_ErrorNone) {
                        printf("Error sending buffer for filling: %x\n", r);
                    }
                }

                if (VERBOSE) {
                    after = cluon::time::now();
                }

                if (0 < totalSize) {
                    opendlv::proxy::ImageReading ir;
                    ir.fourcc("h264").width(WIDTH).height(HEIGHT).data(std::string(&h264Buffer[0], totalSize));
                    od4.send(ir, sampleTimeStamp, ID);

                    if (VERBOSE) {
                        std::clog << "[opendlv-video-h264-omx-encoder-rpi]: Frame size = " << totalSize << " bytes; encoding took " << cluon::time::deltaInMicroseconds(after, before) << " microseconds." << std::endl;
                    }
                }
            }

            ////////////////////////////////////////////////////////////////////
            // Based on: https://github.com/raspberrypi/userland/blob/e5b2684c507442f6a0c7508c34a3f61706dc00e6/host_applications/linux/apps/hello_pi/hello_encode/encode.c#L289-L302
            printf("Teardown.\n");

            printf("disabling port buffers for 200 and 201...\n");
            ilclient_disable_port_buffers(video_encode, 200, NULL, NULL, NULL);
            ilclient_disable_port_buffers(video_encode, 201, NULL, NULL, NULL);

            ilclient_state_transition(list, OMX_StateIdle);
            ilclient_state_transition(list, OMX_StateLoaded);

            ilclient_cleanup_components(list);

            OMX_Deinit();

            ilclient_destroy(client);

            retCode = 0;
        }
        else {
            std::cerr << "[opendlv-video-h264-omx-encoder-rpi]: Failed to attach to shared memory '" << NAME << "'." << std::endl;
        }
    }
    return retCode;
}
