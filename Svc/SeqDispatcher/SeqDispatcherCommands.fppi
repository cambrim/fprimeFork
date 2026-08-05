@ Dispatches a sequence to the first available sequencer
async command RUN(
                      fileName: string size FileNameStringSize @< The name of the sequence file
                      $block: Svc.BlockState @< Return command status when complete or not
                    ) \
    opcode 0

@ Dispatches a sequence with arguments to the first available sequencer
async command RUN_ARGS(
                      fileName: string size FileNameStringSize @< The name of the sequence file
                      $block: Svc.BlockState @< Return command status when complete or not
                      buffer: Svc.SeqArgs @< Arguments to pass to a sequencer
                    ) \
    opcode 1

@ Logs via Events the state of each connected command sequencer
async command LOG_STATUS() opcode 2

@ Cancels any running sequence matching the given file name
async command CANCEL_NAME(
                      fileName: string size FileNameStringSize @< The name of the sequence file to cancel
                    ) \
    opcode 3

@ Broadcast a cancel to every running sequencer. This does not exclude the caller!
@ A sequence issuing CANCEL_ALL will cancel itself is connected to this seqDispatcher.
async command CANCEL_ALL() opcode 4

@ Clear all queued sequences
async command CLEAR_QUEUE() opcode 5

@ List all queued sequences
async command LIST_QUEUE() opcode 6

@ Get queue status (depth and statistics)
async command GET_QUEUE_STATUS() opcode 7

@ Pause queue processing (running sequences continue, but queue won't dispatch)
async command PAUSE_QUEUE() opcode 8

@ Resume queue processing
async command RESUME_QUEUE() opcode 9
