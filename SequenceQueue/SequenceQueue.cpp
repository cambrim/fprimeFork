// ======================================================================
// \title  SequenceQueue.cpp
// \author cambrim
// \brief  cpp file for SequenceQueue component implementation class
// ======================================================================

#include "OxusFsw/OxusFsw/Components/SequenceQueue/SequenceQueue.hpp"

namespace OxusFsw {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

SequenceQueue::SequenceQueue(const char *const compName)
    : SequenceQueueComponentBase(compName),
      m_queueState(SequenceQueueStateType::STOPPED),
      m_sequencesCompleted(0),
      m_sequencesFailed(0),
      m_isRunning(false),
      m_maxQueueDepth(500) // Default, will be overridden by parameter
{}

SequenceQueue::~SequenceQueue() {}


// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void SequenceQueue::schedRun_handler(FwIndexType portNum, U32 context) {
    // Update telemetry
    this->tlmWrite_QueueDepth(static_cast<U32>(m_sequenceQueue.size()));
    this->tlmWrite_IsRunning(m_isRunning);
    this->tlmWrite_QueueState(m_queueState);
    this->tlmWrite_SequencesCompleted(m_sequencesCompleted);
    this->tlmWrite_SequencesFailed(m_sequencesFailed);
}

void SequenceQueue::seqDoneIn_handler(FwIndexType portNum, 
                                      FwOpcodeType opCode,
                                      U32 cmdSeq,
                                      const Fw::CmdResponse &response) {
    // Sequence completed (successfully or with error)
    m_isRunning = false;

    if (response.e == Fw::CmdResponse::OK) {
        m_sequencesCompleted++;
    } else {
        m_sequencesFailed++;
    }

    // Try to run the next sequence if queue is in RUNNING state
    if (m_queueState == SequenceQueueStateType::RUNNING) {
        runNextSequence();
    }
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void SequenceQueue::QUEUE_SEQUENCE_cmdHandler(
    FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdStringArg &sequencePath) {
    
    // Load max queue depth from parameter
    Fw::ParamValid valid;
    U32 maxDepth = paramGet_MAX_QUEUE_DEPTH(valid);
    if (valid == Fw::ParamValid::VALID) {
        m_maxQueueDepth = maxDepth;
    }

    // Check if queue is full
    if (m_sequenceQueue.size() >= m_maxQueueDepth) {
        this->log_WARNING_HI_QueueOverflow(sequencePath);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    // Add sequence to queue
    m_sequenceQueue.push(Fw::String(sequencePath.toChar()));
    
    // Emit event
    this->log_ACTIVITY_LO_SequenceQueued(sequencePath, 
                                         static_cast<U32>(m_sequenceQueue.size()));

    // If queue was stopped, transition to RUNNING
    if (m_queueState == SequenceQueueStateType::STOPPED) {
        m_queueState = SequenceQueueStateType::RUNNING;
    }

    // If nothing is currently running and queue is RUNNING, start the sequence
    if (!m_isRunning && m_queueState == SequenceQueueStateType::RUNNING) {
        runNextSequence();
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SequenceQueue::CLEAR_QUEUE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    U32 numCleared = static_cast<U32>(m_sequenceQueue.size());
    
    // Clear the queue
    while (!m_sequenceQueue.empty()) {
        m_sequenceQueue.pop();
    }

    // Emit event
    this->log_ACTIVITY_HI_QueueCleared(numCleared);

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SequenceQueue::GET_QUEUE_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // Status is reported via events
    if (m_sequenceQueue.empty()) {
        this->log_ACTIVITY_LO_QueueEmpty();
    } else {
        // Report queue depth via event (reusing SequenceQueued event format)
        Fw::String statusMsg = "Queue status";
        this->log_ACTIVITY_LO_SequenceQueued(statusMsg, 
                                             static_cast<U32>(m_sequenceQueue.size()));
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SequenceQueue::PAUSE_QUEUE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (m_queueState != SequenceQueueStateType::PAUSED) {
        m_queueState = SequenceQueueStateType::PAUSED;
        this->log_ACTIVITY_HI_QueuePaused();
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SequenceQueue::RESUME_QUEUE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (m_queueState == SequenceQueueStateType::PAUSED) {
        m_queueState = SequenceQueueStateType::RUNNING;
        this->log_ACTIVITY_HI_QueueResumed();
        
        // Try to run next sequence if not currently running
        if (!m_isRunning) {
            runNextSequence();
        }
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SequenceQueue::LIST_QUEUE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // For now, just report the queue depth
    // Future enhancement: iterate and log each sequence path
    U32 queueDepth = static_cast<U32>(m_sequenceQueue.size());
    Fw::String listMsg = "Listing queue";
    this->log_ACTIVITY_LO_SequenceQueued(listMsg, queueDepth);

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Private helper methods
// ----------------------------------------------------------------------

void SequenceQueue::runNextSequence() {
    // Check if there are sequences to run
    if (m_sequenceQueue.empty()) {
        this->log_ACTIVITY_LO_QueueEmpty();
        m_queueState = SequenceQueueStateType::STOPPED;
        return;
    }

    // Check if queue is paused
    if (m_queueState == SequenceQueueStateType::PAUSED) {
        return;
    }

    // Get next sequence
    Fw::String nextSequence = m_sequenceQueue.front();
    m_sequenceQueue.pop();

    // Mark as running
    m_isRunning = true;

    // Emit event
    this->log_ACTIVITY_HI_StartingSequence(nextSequence);

    // Send sequence run command to cmdSeq
    if (isConnected_seqRunOut_OutputPort(0)) {
        Svc::SeqArgs emptyArgs;
        this->seqRunOut_out(0, nextSequence, emptyArgs);
    }
}

} // namespace OxusFsw