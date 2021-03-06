/*
 * Copyright (c) 2018, Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "runtime/command_stream/command_stream_receiver_hw.h"
#include "runtime/command_stream/experimental_command_buffer.h"
#include "runtime/command_stream/linear_stream.h"
#include "runtime/memory_manager/graphics_allocation.h"

namespace OCLRT {

template <typename GfxFamily>
void ExperimentalCommandBuffer::injectBufferStart(LinearStream &parentStream, size_t cmdBufferOffset) {
    using MI_BATCH_BUFFER_START = typename GfxFamily::MI_BATCH_BUFFER_START;
    auto pCmd = static_cast<MI_BATCH_BUFFER_START *>(parentStream.getSpace(sizeof(MI_BATCH_BUFFER_START)));
    auto commandStreamReceiverHw = reinterpret_cast<CommandStreamReceiverHw<GfxFamily> *>(commandStreamReceiver);
    commandStreamReceiverHw->addBatchBufferStart(pCmd, currentStream->getGraphicsAllocation()->getGpuAddress() + cmdBufferOffset, true);
}

template <typename GfxFamily>
size_t ExperimentalCommandBuffer::getRequiredInjectionSize() noexcept {
    using MI_BATCH_BUFFER_START = typename GfxFamily::MI_BATCH_BUFFER_START;
    return sizeof(MI_BATCH_BUFFER_START);
}

template <typename GfxFamily>
size_t ExperimentalCommandBuffer::programExperimentalCommandBuffer() {
    using MI_BATCH_BUFFER_END = typename GfxFamily::MI_BATCH_BUFFER_END;

    getCS(getTotalExperimentalSize<GfxFamily>());

    size_t returnOffset = currentStream->getUsed();

    //begin timestamp
    addTimeStampPipeControl<GfxFamily>();

    addExperimentalCommands<GfxFamily>();

    //end timestamp
    addTimeStampPipeControl<GfxFamily>();

    //end
    auto pCmd = static_cast<MI_BATCH_BUFFER_END *>(currentStream->getSpace(sizeof(MI_BATCH_BUFFER_END)));
    *pCmd = GfxFamily::cmdInitBatchBufferEnd;

    return returnOffset;
}

template <typename GfxFamily>
size_t ExperimentalCommandBuffer::getTotalExperimentalSize() noexcept {
    using MI_BATCH_BUFFER_END = typename GfxFamily::MI_BATCH_BUFFER_END;

    size_t size = sizeof(MI_BATCH_BUFFER_END) + getTimeStampPipeControlSize<GfxFamily>() + getExperimentalCommandsSize<GfxFamily>();
    return size;
}

template <typename GfxFamily>
size_t ExperimentalCommandBuffer::getTimeStampPipeControlSize() noexcept {
    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;

    //two P_C for timestamps, two prior them for WA - Enable CS Stall
    return 4 * sizeof(PIPE_CONTROL);
}

template <typename GfxFamily>
void ExperimentalCommandBuffer::addTimeStampPipeControl() {
    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;

    auto pCmd = static_cast<PIPE_CONTROL *>(currentStream->getSpace(sizeof(PIPE_CONTROL)));
    *pCmd = GfxFamily::cmdInitPipeControl;
    pCmd->setCommandStreamerStallEnable(true);

    uint64_t timeStampAddress = timestamps->getGpuAddress() + timestampsOffset;

    pCmd = static_cast<PIPE_CONTROL *>(currentStream->getSpace(sizeof(PIPE_CONTROL)));
    *pCmd = GfxFamily::cmdInitPipeControl;
    pCmd->setCommandStreamerStallEnable(true);
    pCmd->setPostSyncOperation(PIPE_CONTROL::POST_SYNC_OPERATION_WRITE_TIMESTAMP);
    pCmd->setAddress(static_cast<uint32_t>(timeStampAddress & 0x0000FFFFFFFFULL));
    pCmd->setAddressHigh(static_cast<uint32_t>(timeStampAddress >> 32));

    //moving to next chunk
    timestampsOffset += sizeof(uint64_t);

    DEBUG_BREAK_IF(timestamps->getUnderlyingBufferSize() < timestampsOffset);
}

template <typename GfxFamily>
void ExperimentalCommandBuffer::addExperimentalCommands() {
    using MI_SEMAPHORE_WAIT = typename GfxFamily::MI_SEMAPHORE_WAIT;

    uintptr_t semaphoreAddr = reinterpret_cast<uintptr_t>(experimentalAllocation->getUnderlyingBuffer()) + experimentalAllocationOffset;
    uint32_t *semaphoreData = reinterpret_cast<uint32_t *>(semaphoreAddr);
    *semaphoreData = 1;
    uint64_t gpuAddr = experimentalAllocation->getGpuAddress() + experimentalAllocationOffset;

    auto semaphoreCmd = reinterpret_cast<MI_SEMAPHORE_WAIT *>(currentStream->getSpace(sizeof(MI_SEMAPHORE_WAIT)));
    *semaphoreCmd = MI_SEMAPHORE_WAIT::sInit();
    semaphoreCmd->setCompareOperation(MI_SEMAPHORE_WAIT::COMPARE_OPERATION_SAD_EQUAL_SDD);
    semaphoreCmd->setSemaphoreDataDword(*semaphoreData);
    semaphoreCmd->setSemaphoreGraphicsAddress(gpuAddr);
}

template <typename GfxFamily>
size_t ExperimentalCommandBuffer::getExperimentalCommandsSize() noexcept {
    using MI_SEMAPHORE_WAIT = typename GfxFamily::MI_SEMAPHORE_WAIT;
    return sizeof(MI_SEMAPHORE_WAIT);
}

} // namespace OCLRT
