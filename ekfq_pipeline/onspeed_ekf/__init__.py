"""OnSpeed EKFQ — Python reference implementation and Optuna tuning pipeline.

`EKFQ` is the 11-state quaternion EKF whose tuned defaults the firmware in
`software/Libraries/onspeed_core/src/ahrs/EKFQ.{h,cpp}` consumes. The
Python version is the algorithm reference; the firmware port preserves
its math exactly so any retune done here drops straight into a new
config-file <EKFQ> section.

State vector:  x = [q0, q1, q2, q3, bp, bq, br, z, vz, b_az, β]
"""

from .data import FlightLog, load_log
from .ekf_quat import EKFQ, EKFQConfig, EKFQState, GRAVITY_MPS2
from .pipeline_quat import PipelineQuat, PipelineQuatConfig

__all__ = [
    "FlightLog", "load_log",
    "EKFQ", "EKFQConfig", "EKFQState", "GRAVITY_MPS2",
    "PipelineQuat", "PipelineQuatConfig",
]
