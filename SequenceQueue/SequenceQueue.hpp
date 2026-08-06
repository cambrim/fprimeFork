// ======================================================================
// \title  SequenceQueue.hpp
// \author cambrim
// \brief  hpp file for SequenceQueue component implementation class
// ======================================================================

#ifndef OxusFsw_SequenceQueue_HPP
#define OxusFsw_SequenceQueue_HPP
#include <queue>

#include "OxusFsw/OxusFsw/Components/SequenceQueue/SequenceQueueComponentAc.hpp"

namespace OxusFsw {

class SequenceQueue final : public SequenceQueueComponentBase {

public:
  // ----------------------------------------------------------------------
  // Component construction and destruction
  // ----------------------------------------------------------------------

  //! Construct SequenceQueue object
  SequenceQueue(const char *const compName //!< The component name
  );

  //! Destroy SequenceQueue object
  ~SequenceQueue();

private:
  // ----------------------------------------------------------------------
  // Handler implementations for typed input ports
  // ----------------------------------------------------------------------

  //! Handler implementation for schedRun
  //!
  //! Receiving calls from the rate group (for telemetry updates)
  void schedRun_handler(FwIndexType portNum, //!< The port number
                        U32 context          //!< The call order
                        ) override;

  //! Handler implementation for seqDoneIn
  //!
  //! Port to receive sequence done notifications from cmdSeq
  void seqDoneIn_handler(
      FwIndexType portNum,            //!< The port number
      FwOpcodeType opCode,            //!< Command Op Code
      U32 cmdSeq,                     //!< Command Sequence
      const Fw::CmdResponse &response //!< The command response argument
      ) override;

private:
  // ----------------------------------------------------------------------
  // Handler implementations for commands
  // ----------------------------------------------------------------------

  //! Handler implementation for command QUEUE_SEQUENCE
  //!
  //! Queue a sequence to run
  void QUEUE_SEQUENCE_cmdHandler(
      FwOpcodeType opCode,                 //!< The opcode
      U32 cmdSeq,                          //!< The command sequence number
      const Fw::CmdStringArg &sequencePath //!< Path to sequence file
      ) override;

  //! Handler implementation for command CLEAR_QUEUE
  //!
  //! Clear all queued sequences
  void CLEAR_QUEUE_cmdHandler(FwOpcodeType opCode, //!< The opcode
                              U32 cmdSeq //!< The command sequence number
                              ) override;

  //! Handler implementation for command GET_QUEUE_STATUS
  //!
  //! Get current queue status
  void GET_QUEUE_STATUS_cmdHandler(FwOpcodeType opCode, //!< The opcode
                                   U32 cmdSeq //!< The command sequence number
                                   ) override;

  //! Handler implementation for command PAUSE_QUEUE
  //!
  //! Pause sequence execution
  void PAUSE_QUEUE_cmdHandler(FwOpcodeType opCode, //!< The opcode
                              U32 cmdSeq //!< The command sequence number
                              ) override;

  //! Handler implementation for command RESUME_QUEUE
  //!
  //! Resume sequence execution
  void RESUME_QUEUE_cmdHandler(FwOpcodeType opCode, //!< The opcode
                               U32 cmdSeq //!< The command sequence number
                               ) override;

  //! Handler implementation for command LIST_QUEUE
  //!
  //! Get detailed queue status with all pending sequences
  void LIST_QUEUE_cmdHandler(FwOpcodeType opCode, //!< The opcode
                             U32 cmdSeq //!< The command sequence number
                             ) override;

private:
  // ----------------------------------------------------------------------
  // Private helper methods
  // ----------------------------------------------------------------------

  //! Try to run the next sequence in the queue
  void runNextSequence();

private:
  // ----------------------------------------------------------------------
  // Member variables
  // ----------------------------------------------------------------------

  //! Queue of sequence paths waiting to be executed
  std::queue<Fw::String> m_sequenceQueue;

  //! Current state of the queue
  SequenceQueueStateType m_queueState;

  //! Counter for completed sequences
  U32 m_sequencesCompleted;

  //! Counter for failed sequences
  U32 m_sequencesFailed;

  //! Is a sequence currently running
  bool m_isRunning;

  //! Maximum queue depth from parameter
  U32 m_maxQueueDepth;                             
                            };

} // namespace OxusFsw

#endif
