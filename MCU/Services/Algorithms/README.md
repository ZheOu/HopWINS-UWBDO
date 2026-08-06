# Service Algorithms

Algorithms consume structured service data and must not call STM32 HAL, Board,
Console, or device-driver APIs.

## Timestamp strategies

`Timestamp/uwb_timestamp_estimator.c` is the selector and registry.
`Timestamp/uwb_timestamp_dw.c` contains the two stateless native DW3000
strategies. To add a dual-antenna CIR estimator:

1. Add one implementation file with a strategy descriptor.
2. Register the descriptor in `s_strategies`.
3. Add its ID to `uwb_timestamp_estimator_id_t`.
4. Set `requires_cir=true`; copy any CIR data needed after `process()` returns,
   because the workflow releases the capture slot after encoding.
5. Return `UWB_TIMESTAMP_RESULT_PENDING` until an RF1/RF2 pair is complete.

The Follower workflow does not need to know whether an estimator is stateless,
cross-correlates multiple frames, or combines both RF paths.

## DO loop strategies

`ClockDiscipline/do_loop_strategy.c` estimates residual frequency error but
does not touch an oscillator. `Workflows/DO/do_clock_tracking_service.c`
validates HWDO frames, applies limits, and sends the selected strategy's result
to `clock_service_set_pull_ppb()`.

Keep experimental estimators as separate strategy IDs so the UART configuration
record identifies exactly which result produced each run.
