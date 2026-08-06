module OxusFsw {
    
    @ Sequence Queue execution state
    enum SequenceQueueStateType {
        STOPPED = 0     @< Queue is stopped, not processing sequences
        RUNNING = 1     @< Queue is actively running sequences
        PAUSED = 2      @< Queue is paused, will resume later
    }

    @ Component that manages a queue of command sequences
    @ and automatically runs them one after another
    active component SequenceQueue {

        ##############################################################################
        # Commands                                                                   #
        ##############################################################################

        @ Queue a sequence to run
        async command QUEUE_SEQUENCE(
            sequencePath: string size 256 @< Path to sequence file
        )

        @ Clear all queued sequences
        async command CLEAR_QUEUE()

        @ Get current queue status
        async command GET_QUEUE_STATUS()

        @ Pause sequence execution
        async command PAUSE_QUEUE()

        @ Resume sequence execution
        async command RESUME_QUEUE()

        @ Get detailed queue status with all pending sequences
        async command LIST_QUEUE()


        ##############################################################################
        # Telemetry                                                                  #
        ##############################################################################

        @ Current number of sequences in queue
        telemetry QueueDepth: U32

        @ Is a sequence currently running
        telemetry IsRunning: bool
        
        @ Current queue state (running/paused/stopped)
        telemetry QueueState: SequenceQueueStateType

        @ Number of sequences completed this session
        telemetry SequencesCompleted: U32

        @ Number of sequences failed this session
        telemetry SequencesFailed: U32

        ##############################################################################
        # Events (EVRs)                                                              #
        ############################################################################## 

        @ Sequence was added to queue
        event SequenceQueued(
            path: string size 256 @< Sequence path
            queueDepth: U32 @< Current queue depth
        ) \
            severity activity low \
            format "Queued sequence: {} (queue depth: {})"

        @ Starting next sequence
        event StartingSequence(
            path: string size 256 @< Sequence path
        ) \
            severity activity high \
            format "Starting sequence: {}"

        @ Queue paused
        event QueuePaused() \
            severity activity high \
            format "Sequence queue paused" 
        

        @ Queue resumed
        event QueueResumed() \
            severity activity high \
            format "Sequence queue resumed" 
        

        @ Queue is empty
        event QueueEmpty() \
            severity activity low \
            format "Sequence queue is empty"

        @ Queue overflow - sequence discarded
        event QueueOverflow(
            path: string size 256 @< Discarded sequence
        ) \
            severity warning high \
            format "Queue full, discarded sequence: {}"

        @ Queue cleared
        event QueueCleared(
            numCleared: U32 @< Number of sequences cleared
        ) \
            severity activity high \
            format "Cleared {} sequences from queue"

        ##############################################################################
        # Parameters                                                                 #
        ##############################################################################

        @ Maximum queue depth
        param MAX_QUEUE_DEPTH: U32 default 500

        ##############################################################################
        # Component Ports                                                            #
        ##############################################################################

        @ Receiving calls from the rate group (for telemetry updates)
        sync input port schedRun: Svc.Sched

        @ Port to receive sequence done notifications from cmdSeq
        async input port seqDoneIn: Fw.CmdResponse

        @ Port to send sequence run requests to cmdSeq
        output port seqRunOut: Svc.CmdSeqIn

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables command handling
        import Fw.Command

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel

        @ Port to return the value of a parameter
        param get port prmGetOut

        @Port to set the value of a parameter
        param set port prmSetOut
    }
}