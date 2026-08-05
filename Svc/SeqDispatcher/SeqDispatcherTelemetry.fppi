@ Number of sequences dispatched
telemetry dispatchedCount: U32
@ Number of sequences dispatched that returned an error. Note: if a sequence
@ was run in non-blocking mode, even if the sequence errors out, this error
@ count will never increase
telemetry errorCount: U32
@ Number of sequencers in an available state
telemetry sequencersAvailable: U32
@ Number of sequences canceled by the CANCEL_NAME and CANCEL_ALL commands
telemetry canceledCount: U32
@ Current number of sequences in queue
telemetry queueDepth: U32
@ Total number of sequences queued this session
telemetry queuedTotal: U32
@ Number of sequences executed from queue
telemetry sequencesExecutedFromQueue: U32
@ Number of times queue overflowed (rejected sequences)
telemetry queueOverflows: U32