// GENERATED FILE -- DO NOT EDIT.
// Regenerate with:  python -m onspeed_ekf.symbolic
// Source of truth:  ekfq_pipeline/onspeed_ekf/symbolic.py
//
// EKFQ predict Jacobian (F) delta-perturbations and accelerometer
// measurement Jacobian (H) entries, derived symbolically.
//
// F = I + delta_F. Field names match the EKFQ.cpp predict()/correct()
// locals so a later PR can replace the hand-written expressions with
// calls to these fillers mechanically. F is the Jacobian of the LINEAR
// quaternion mean (q + q_dot*dt) by design; the beta row uses the R21
// polynomial form, so every entry below is polynomial -- the header is
// core-pure (no <math.h>, no pow()).
//
// NOT YET INCLUDED BY THE FIRMWARE. A follow-up PR slots it into
// EKFQ.cpp predict()/correct() and deletes the hand expressions.

#ifndef ONSPEED_CORE_AHRS_EKFQ_JACOBIANS_GENERATED_H
#define ONSPEED_CORE_AHRS_EKFQ_JACOBIANS_GENERATED_H

namespace onspeed {

// Delta-perturbation entries of the predict Jacobian F = I + delta_F.
// Quaternion block (a01..a32), quaternion-bias block (qb_*),
// vertical channel (vz_*, z_*), and the tas-gated beta row (beta_*).
struct EkfqFJacobian {
    float a01;
    float a02;
    float a03;
    float a10;
    float a12;
    float a13;
    float a20;
    float a21;
    float a23;
    float a30;
    float a31;
    float a32;
    float qb_q0_bp;
    float qb_q0_bq;
    float qb_q0_br;
    float qb_q1_bp;
    float qb_q1_bq;
    float qb_q1_br;
    float qb_q2_bp;
    float qb_q2_bq;
    float qb_q2_br;
    float qb_q3_bp;
    float qb_q3_bq;
    float qb_q3_br;
    float vz_q0;
    float vz_q1;
    float vz_q2;
    float vz_q3;
    float vz_baz;
    float z_q0;
    float z_q1;
    float z_q2;
    float z_q3;
    float z_vz;
    float z_baz;
    float beta_q0;
    float beta_q1;
    float beta_q2;
    float beta_q3;
    float beta_br_dt;
};

// Fill the F delta-perturbation entries. Inputs are the same locals
// EKFQ.cpp::predict computes: bias-corrected gyros are folded into the
// expressions via (p,q,r) and (bp,bq,br); half_dt = 0.5*dt is implicit
// in the symbolic dt. Pass raw gyros (p,q,r), gyro biases (bp,bq,br),
// quaternion (q0..q3), raw accels (ax_raw,ay_raw,az_raw), tas, and dt.
// Caller is responsible for the tas gate: when tas <= tas_min the beta
// row entries (beta_q0..beta_q3, beta_br_dt) must be treated as zero.
static inline EkfqFJacobian ekfqFJacobian(
        float q0, float q1, float q2, float q3,
        float bp, float bq, float br,
        float p, float q, float r,
        float ax_raw, float ay_raw, float az_raw,
        float tas, float dt) {
    EkfqFJacobian F;
    const float x0 = (0.5f)*dt;
    const float x1 = x0*(bp - p);
    const float x2 = x0*(bq - q);
    const float x3 = x0*(br - r);
    const float x4 = -x1;
    const float x5 = -x3;
    const float x6 = -x2;
    const float x7 = q1*x0;
    const float x8 = q2*x0;
    const float x9 = q3*x0;
    const float x10 = -q0*x0;
    const float x11 = -ax_raw*q2 + ay_raw*q1 + az_raw*q0;
    const float x12 = 2.0f*dt;
    const float x13 = ax_raw*q3 + ay_raw*q0 - az_raw*q1;
    const float x14 = ax_raw*q0 - ay_raw*q3 + az_raw*q2;
    const float x15 = ax_raw*q1 + ay_raw*q2 + az_raw*q3;
    const float x16 = -dt;
    const float x17 = (dt*dt);
    const float x18 = 19.613299999999999f*dt/tas;
    F.a01 = x1;
    F.a02 = x2;
    F.a03 = x3;
    F.a10 = x4;
    F.a12 = x5;
    F.a13 = x2;
    F.a20 = x6;
    F.a21 = x3;
    F.a23 = x4;
    F.a30 = x5;
    F.a31 = x6;
    F.a32 = x1;
    F.qb_q0_bp = x7;
    F.qb_q0_bq = x8;
    F.qb_q0_br = x9;
    F.qb_q1_bp = x10;
    F.qb_q1_bq = x9;
    F.qb_q1_br = -x8;
    F.qb_q2_bp = -x9;
    F.qb_q2_bq = x10;
    F.qb_q2_br = x7;
    F.qb_q3_bp = x8;
    F.qb_q3_bq = -x7;
    F.qb_q3_br = x10;
    F.vz_q0 = x11*x12;
    F.vz_q1 = x12*x13;
    F.vz_q2 = -x12*x14;
    F.vz_q3 = x12*x15;
    F.vz_baz = x16;
    F.z_q0 = -x11*x17;
    F.z_q1 = -x13*x17;
    F.z_q2 = x14*x17;
    F.z_q3 = -x15*x17;
    F.z_vz = x16;
    F.z_baz = (0.5f)*x17;
    F.beta_q0 = q1*x18;
    F.beta_q1 = q0*x18;
    F.beta_q2 = q3*x18;
    F.beta_q3 = q2*x18;
    F.beta_br_dt = dt;
    return F;
}

// Partials of the earth-down inertial accel a_D w.r.t. the quaternion.
// EKFQ.cpp uses these as named intermediates: vz_q* = dt*dD_q*,
// z_q* = -0.5*dt*dt*dD_q*. Exposed for parity / documentation.
struct EkfqADPartials {
    float dD_q0;
    float dD_q1;
    float dD_q2;
    float dD_q3;
};
static inline EkfqADPartials ekfqADPartials(
        float q0, float q1, float q2, float q3,
        float ax_raw, float ay_raw, float az_raw) {
    EkfqADPartials d;
    d.dD_q0 = 2.0f*(-ax_raw*q2 + ay_raw*q1 + az_raw*q0);
    d.dD_q1 = 2.0f*(ax_raw*q3 + ay_raw*q0 - az_raw*q1);
    d.dD_q2 = 2.0f*(-ax_raw*q0 + ay_raw*q3 - az_raw*q2);
    d.dD_q3 = 2.0f*(ax_raw*q1 + ay_raw*q2 + az_raw*q3);
    return d;
}

// Non-zero entries of the accelerometer measurement Jacobian H (3x11).
// ax/ay/az rows; quaternion columns plus the centripetal bias couplings
// ay_br = d(ay_pred)/d(br) = -tas and az_bq = d(az_pred)/d(bq) = +tas.
struct EkfqHJacobian {
    float ax_q0;
    float ax_q1;
    float ax_q2;
    float ax_q3;
    float ay_q0;
    float ay_q1;
    float ay_q2;
    float ay_q3;
    float ay_br;
    float az_q0;
    float az_q1;
    float az_q2;
    float az_q3;
    float az_bq;
};

// Fill the H entries. Pass the quaternion (q0..q3) and tas. The accel
// predictions also depend on (q,r,bq,br,tasdot) but H's non-zero entries
// are functions of (q0..q3) and tas only.
static inline EkfqHJacobian ekfqHJacobian(
        float q0, float q1, float q2, float q3, float tas) {
    EkfqHJacobian H;
    const float x0 = 19.613299999999999f*q2;
    const float x1 = -19.613299999999999f*q3;
    const float x2 = 19.613299999999999f*q0;
    const float x3 = 19.613299999999999f*q1;
    const float x4 = -x3;
    const float x5 = -x2;
    H.ax_q0 = x0;
    H.ax_q1 = x1;
    H.ax_q2 = x2;
    H.ax_q3 = x4;
    H.ay_q0 = x4;
    H.ay_q1 = x5;
    H.ay_q2 = x1;
    H.ay_q3 = -x0;
    H.ay_br = -tas;
    H.az_q0 = x5;
    H.az_q1 = x3;
    H.az_q2 = x0;
    H.az_q3 = x1;
    H.az_bq = tas;
    return H;
}

}  // namespace onspeed

#endif  // ONSPEED_CORE_AHRS_EKFQ_JACOBIANS_GENERATED_H
