"""SymPy source of truth for the EKFQ predict and measurement Jacobians.

This module is the single symbolic definition of the OnSpeed EKFQ filter's
predict state-transition Jacobian ``F`` and accelerometer measurement
Jacobian ``H``. Both are derived by symbolic differentiation of pure
state-and-input functions:

    F = jacobian( f_mean_linear(x, u, dt) )   w.r.t. the 11-state vector
    H = jacobian( h_accels(x, u) )            w.r.t. the 11-state vector

State layout (matches ``ekf_quat.py`` / ``EKFQ.cpp`` exactly), indices 0..10:

    x = [q0, q1, q2, q3, bp, bq, br, z, vz, b_az, beta]

Predict/measurement input vector u (8 elements):

    u = [p, q, r, ax_raw, ay_raw, az_raw, tas, tasdot]

F IS THE JACOBIAN OF THE LINEAR MEAN, BY DESIGN
-----------------------------------------------
The runtime predict MEAN advances the quaternion with the exact exp-map step
(``q <- q (x) exp(0.5 w dt)``), but ``F`` is deliberately the first-order
Jacobian of the LINEAR mean ``q + q_dot*dt`` (see ``_mean_linear`` below). The
covariance linearization is first-order; the exact map's extra terms are
O((w*dt)^3), which the covariance does not track. So we differentiate
``f_mean_linear``, NOT the exact map, to obtain F. This matches the hand-written
F in ``ekf_quat.py::EKFQ.predict`` and ``EKFQ.cpp::predict``.

BETA ROW USES THE R21-POLYNOMIAL FORM
-------------------------------------
The sideslip-rate beta-channel partials are derived from the algebraic form

    beta_dot = (ay_raw + 2*(q2*q3 + q0*q1)*g) / tas - r_c

i.e. using R21 = 2*(q2*q3 + q0*q1) directly, NOT the equivalent
``sin(roll)*cos(theta)`` form. The two forms are equal in VALUE on the unit
quaternion manifold, but their q-gradients differ off-manifold, so they produce
different Jacobian beta rows. The firmware (EKFQ.cpp) uses the R21 form, which
is purely polynomial and therefore core-pure (no sin/cos/atan2/sqrt in the
generated C header). This module follows the firmware. The resulting beta-row
partials equal the firmware's ``beta_q0..beta_q3 = (g/tas)*dt*2*{q1,q0,q3,q2}``.

The tas-gate (beta dynamics damped below ``tas_min``) is a RUNTIME branch, not
part of the symbolic derivative. The symbolic beta row is always the active
(in-air) expression; the oracle compares against the hand-F, which zeroes the
beta row when ``tas <= tas_min``.

REGENERATING THE ARTIFACTS
--------------------------
Run::

    cd ekfq_pipeline
    uv run --group dev python3 -m onspeed_ekf.symbolic

This emits two generated files (both carry a DO-NOT-EDIT banner):

    onspeed_ekf/_generated_jacobians.py
        Self-contained NumPy module exposing F(x, u, dt) -> (11,11) and
        H(x, u) -> (3,11). Imports numpy only, no sympy at runtime.

    ../software/Libraries/onspeed_core/src/ahrs/EKFQ_jacobians.generated.h
        Core-pure C header (no <math.h>, no pow()) exposing static-inline
        functions that fill structs of the F delta-perturbation entries and
        the H entries, named to match the EKFQ.cpp predict()/correct() locals
        so a later PR can slot it in mechanically.
"""

from __future__ import annotations

import os

import sympy as sp
from sympy.printing.c import C99CodePrinter

# ---------------------------------------------------------------------------
# Symbols
# ---------------------------------------------------------------------------

GRAVITY = sp.Float(9.80665)

# State symbols (indices 0..10).
q0, q1, q2, q3 = sp.symbols("q0 q1 q2 q3", real=True)
bp, bq, br = sp.symbols("bp bq br", real=True)
z, vz, b_az = sp.symbols("z vz b_az", real=True)
beta = sp.symbols("beta", real=True)

# Input symbols.
p, q, r = sp.symbols("p q r", real=True)
ax_raw, ay_raw, az_raw = sp.symbols("ax_raw ay_raw az_raw", real=True)
tas, tasdot = sp.symbols("tas tasdot", real=True)
dt = sp.symbols("dt", real=True)

# State vector, in layout order.
STATE = sp.Matrix([q0, q1, q2, q3, bp, bq, br, z, vz, b_az, beta])

# State / input names in their canonical orders (used by the emitters).
STATE_NAMES = ["q0", "q1", "q2", "q3", "bp", "bq", "br", "z", "vz", "b_az", "beta"]
INPUT_NAMES = ["p", "q", "r", "ax_raw", "ay_raw", "az_raw", "tas", "tasdot"]


# ---------------------------------------------------------------------------
# Predict mean (linear) and measurement model
# ---------------------------------------------------------------------------


def _mean_linear() -> sp.Matrix:
    """Length-11 propagated state using the LINEAR quaternion step.

    Quaternion: q <- q + q_dot*dt with q_dot = 0.5*W(omega)*q.
    Biases (bp, bq, br) and b_az pass through unchanged.
    Vertical channel (z, vz) integrates the earth-down inertial accel a_D.
    Beta uses the R21-polynomial form (un-gated; see module docstring).

    F = jacobian of this vector. This is the first-order Jacobian of the LINEAR
    mean by design, even though the runtime mean uses the exact exp-map.
    """
    # Bias-corrected gyros.
    p_c = p - bp
    q_c = q - bq
    r_c = r - br

    # Linear mean q_dot from q_dot = 0.5*W(omega)*q.
    q0d = sp.Rational(1, 2) * (-q1 * p_c - q2 * q_c - q3 * r_c)
    q1d = sp.Rational(1, 2) * (q0 * p_c + q2 * r_c - q3 * q_c)
    q2d = sp.Rational(1, 2) * (q0 * q_c - q1 * r_c + q3 * p_c)
    q3d = sp.Rational(1, 2) * (q0 * r_c + q1 * q_c - q2 * p_c)

    q0_new = q0 + q0d * dt
    q1_new = q1 + q1d * dt
    q2_new = q2 + q2d * dt
    q3_new = q3 + q3d * dt

    # Earth-down inertial acceleration: R_be[2,:]*a_body + g - b_az.
    R20 = 2 * (q1 * q3 - q0 * q2)
    R21 = 2 * (q2 * q3 + q0 * q1)
    R22 = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3
    a_D = R20 * ax_raw + R21 * ay_raw + R22 * az_raw + GRAVITY - b_az

    z_new = z - (vz + sp.Rational(1, 2) * a_D * dt) * dt
    vz_new = vz + a_D * dt

    # Beta dynamics, R21-polynomial form (no transcendentals); un-gated.
    beta_dot = (ay_raw + R21 * GRAVITY) / tas - r_c
    beta_new = beta + dt * beta_dot

    return sp.Matrix([
        q0_new, q1_new, q2_new, q3_new,
        bp, bq, br,
        z_new, vz_new, b_az,
        beta_new,
    ])


def _h_accels() -> sp.Matrix:
    """Length-3 predicted body specific force (ax_pred, ay_pred, az_pred).

    Matches ekf_quat.py / jax_model h_accels and EKFQ.cpp correct() exactly:

        ax_pred = -2*g*(q1*q3 - q0*q2) + tasdot
        ay_pred = -2*g*(q2*q3 + q0*q1) + tas*(r - br)
        az_pred =   -g*(q0^2 - q1^2 - q2^2 + q3^2) - tas*(q - bq)
    """
    q_c = q - bq
    r_c = r - br
    ax_pred = -2 * GRAVITY * (q1 * q3 - q0 * q2) + tasdot
    ay_pred = -2 * GRAVITY * (q2 * q3 + q0 * q1) + tas * r_c
    az_pred = -GRAVITY * (q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3) - tas * q_c
    return sp.Matrix([ax_pred, ay_pred, az_pred])


def F_matrix() -> sp.Matrix:
    """11x11 predict Jacobian = jacobian of the LINEAR mean."""
    return _mean_linear().jacobian(STATE)


def H_matrix() -> sp.Matrix:
    """3x11 measurement Jacobian = jacobian of the accel predictions."""
    return _h_accels().jacobian(STATE)


# ---------------------------------------------------------------------------
# Core-pure C printer: expand integer powers to repeated multiplication so the
# generated header needs no <math.h> / pow(). The F and H expressions are
# polynomial (the R21 beta form keeps the beta row polynomial), so no
# transcendental functions appear.
# ---------------------------------------------------------------------------


class _PurePrinter(C99CodePrinter):
    """C99 printer that never emits pow(); small integer powers expand to
    repeated multiplication. Keeps the generated header core-pure (no math.h)."""

    def _print_Pow(self, expr):
        base = expr.base
        exp = expr.exp
        if exp.is_Integer and exp > 0:
            n = int(exp)
            if n <= 8:
                b = self._print(base)
                return "(" + "*".join([b] * n) + ")"
        # No negative/fractional/large powers occur in F or H. If one ever
        # does, fail loudly rather than silently emitting pow().
        raise ValueError(
            f"_PurePrinter refuses to emit pow() for exponent {exp}; "
            "F/H are expected to be polynomial with small positive integer powers."
        )

    def _print_Float(self, expr):
        # Always emit a float literal with an 'f' suffix so the C code stays
        # single precision and matches the firmware's float arithmetic.
        s = super()._print_Float(expr)
        if not s.endswith("f") and not s.endswith("F"):
            s = s + "f"
        return s

    def _print_Rational(self, expr):
        # Emit rationals as a single-precision float literal (e.g. 0.5f), not a
        # double-typed (1.0/2.0) division. Keeps the header all-float so its
        # arithmetic matches the firmware's single-precision EKFQ.cpp.
        return "%sf" % repr(float(expr.p) / float(expr.q))

    def _print_Integer(self, expr):
        # Bare integers that act as multiplicative coefficients (e.g. the 2 in
        # 2*(q2*q3+q0*q1)) render as single-precision float literals (2.0f) so
        # no int->float promotion happens at runtime and the literal is valid C
        # (a bare "2f" is not a legal suffix). Index-like integers do not occur
        # in F/H bodies.
        return "%.1ff" % float(expr.p)


_printer = _PurePrinter(settings={"precision": 17})


def _ccode(expr) -> str:
    """Print a sympy expression as a core-pure C float expression."""
    return _printer.doprint(expr)


# ---------------------------------------------------------------------------
# Mapping of (row, col) Jacobian entries to the C++ EKFQ.cpp local names.
#
# F is stored as F = I + delta_F. We expose only the delta entries (the
# off-identity perturbations) under the exact local names EKFQ.cpp::predict
# uses, so a later PR can slot the generated struct in mechanically.
# ---------------------------------------------------------------------------

# (c_field_name, row_index, col_index)
F_FIELDS: list[tuple[str, int, int]] = [
    # Quaternion-quaternion block.
    ("a01", 0, 1), ("a02", 0, 2), ("a03", 0, 3),
    ("a10", 1, 0), ("a12", 1, 2), ("a13", 1, 3),
    ("a20", 2, 0), ("a21", 2, 1), ("a23", 2, 3),
    ("a30", 3, 0), ("a31", 3, 1), ("a32", 3, 2),
    # Quaternion-bias block (4 quats x 3 biases).
    ("qb_q0_bp", 0, 4), ("qb_q0_bq", 0, 5), ("qb_q0_br", 0, 6),
    ("qb_q1_bp", 1, 4), ("qb_q1_bq", 1, 5), ("qb_q1_br", 1, 6),
    ("qb_q2_bp", 2, 4), ("qb_q2_bq", 2, 5), ("qb_q2_br", 2, 6),
    ("qb_q3_bp", 3, 4), ("qb_q3_bq", 3, 5), ("qb_q3_br", 3, 6),
    # Vertical-speed row (VZ = index 8).
    ("vz_q0", 8, 0), ("vz_q1", 8, 1), ("vz_q2", 8, 2), ("vz_q3", 8, 3),
    ("vz_baz", 8, 9),
    # Altitude row (Z = index 7).
    ("z_q0", 7, 0), ("z_q1", 7, 1), ("z_q2", 7, 2), ("z_q3", 7, 3),
    ("z_vz", 7, 8), ("z_baz", 7, 9),
    # Beta row (BETA = index 10), tas-gated at runtime.
    ("beta_q0", 10, 0), ("beta_q1", 10, 1),
    ("beta_q2", 10, 2), ("beta_q3", 10, 3),
    ("beta_br_dt", 10, 6),
]

# a_D partials are computed by EKFQ.cpp as named intermediates (dD_q0..dD_q3);
# we expose them too so the generated header documents the shared subexpression.
# These are the partial of a_D w.r.t. each quaternion component. a_D itself is
# not a Jacobian entry; vz_q* = dt*dD_q*, z_q* = -0.5*dt^2*dD_q*.
DD_FIELDS: list[tuple[str, sp.Expr]] = []  # filled in _build_dd()

# H is a 3x11 matrix; only the quaternion columns and the bias couplings are
# non-zero. We expose the named accel-prediction values and the H row entries.
# (c_field_name, meas_row 0..2, col_index)
H_FIELDS: list[tuple[str, int, int]] = [
    # ax row.
    ("ax_q0", 0, 0), ("ax_q1", 0, 1), ("ax_q2", 0, 2), ("ax_q3", 0, 3),
    # ay row (+ br coupling).
    ("ay_q0", 1, 0), ("ay_q1", 1, 1), ("ay_q2", 1, 2), ("ay_q3", 1, 3),
    ("ay_br", 1, 6),
    # az row (+ bq coupling).
    ("az_q0", 2, 0), ("az_q1", 2, 1), ("az_q2", 2, 2), ("az_q3", 2, 3),
    ("az_bq", 2, 5),
]


def _build_dd() -> list[tuple[str, sp.Expr]]:
    """Partials of a_D w.r.t. quaternion (dD_q0..dD_q3), matching EKFQ.cpp."""
    R20 = 2 * (q1 * q3 - q0 * q2)
    R21 = 2 * (q2 * q3 + q0 * q1)
    R22 = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3
    a_D = R20 * ax_raw + R21 * ay_raw + R22 * az_raw + GRAVITY - b_az
    return [
        ("dD_q0", sp.expand(sp.diff(a_D, q0))),
        ("dD_q1", sp.expand(sp.diff(a_D, q1))),
        ("dD_q2", sp.expand(sp.diff(a_D, q2))),
        ("dD_q3", sp.expand(sp.diff(a_D, q3))),
    ]


# ---------------------------------------------------------------------------
# Emitters
# ---------------------------------------------------------------------------

_BANNER_PY = (
    "# GENERATED FILE -- DO NOT EDIT.\n"
    "# Regenerate with:  python -m onspeed_ekf.symbolic\n"
    "# Source of truth:  onspeed_ekf/symbolic.py\n"
)

_BANNER_C = (
    "// GENERATED FILE -- DO NOT EDIT.\n"
    "// Regenerate with:  python -m onspeed_ekf.symbolic\n"
    "// Source of truth:  ekfq_pipeline/onspeed_ekf/symbolic.py\n"
)


def _numpy_function(name: str, args: list[str], matrix: sp.Matrix,
                    shape: tuple[int, int]) -> str:
    """Build a self-contained NumPy function body for a sympy matrix using CSE.

    The function unpacks the state/input arrays into scalars, evaluates the
    CSE'd entries, and assembles a numpy array of the given shape.
    """
    # Flatten the matrix in row-major order; track (row, col) for assembly.
    entries = []
    positions = []
    for i in range(matrix.rows):
        for j in range(matrix.cols):
            entries.append(matrix[i, j])
            positions.append((i, j))

    # CSE over all entries together (deterministic ordering by default).
    replacements, reduced = sp.cse(entries, optimizations="basic")

    lines: list[str] = []
    lines.append(f"def {name}({', '.join(args)}):")
    # Unpack arrays.
    lines.append("    x = _np.asarray(x, dtype=_np.float64)")
    lines.append("    u = _np.asarray(u, dtype=_np.float64)")
    lines.append("    q0, q1, q2, q3, bp, bq, br, z, vz, b_az, beta = "
                 "x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], x[10]")
    lines.append("    p, q, r, ax_raw, ay_raw, az_raw, tas, tasdot = "
                 "u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7]")
    # CSE temporaries. The gravity constant is folded into numeric literals by
    # CSE (e.g. 2*g -> 19.6133), so no separate symbol is emitted.
    for sym, sub in replacements:
        lines.append(f"    {sp.pycode(sym)} = {sp.pycode(sub)}")
    # Assemble output.
    lines.append(f"    out = _np.zeros({shape}, dtype=_np.float64)")
    for (i, j), red in zip(positions, reduced):
        if red == 0:
            continue
        lines.append(f"    out[{i}, {j}] = {sp.pycode(red)}")
    lines.append("    return out")
    return "\n".join(lines)


def emit_numpy(path: str) -> None:
    """Write the self-contained NumPy Jacobian module to ``path``."""
    Fm = F_matrix()
    Hm = H_matrix()

    parts: list[str] = []
    parts.append('"""Generated NumPy EKFQ Jacobians (F and H).\n')
    parts.append("Self-contained: imports numpy only. Do not edit by hand -- this")
    parts.append("file is regenerated from onspeed_ekf/symbolic.py.\n")
    parts.append("Callables:")
    parts.append("    F(x, u, dt) -> (11, 11) numpy array  (predict Jacobian)")
    parts.append("    H(x, u)     -> (3, 11) numpy array   (measurement Jacobian)")
    parts.append("")
    parts.append("State  x = [q0,q1,q2,q3, bp,bq,br, z,vz,b_az, beta]")
    parts.append("Input  u = [p,q,r, ax_raw,ay_raw,az_raw, tas, tasdot]")
    parts.append('"""')
    parts.append("")
    parts.append(_BANNER_PY)
    parts.append("import numpy as _np")
    parts.append("")
    parts.append("")
    parts.append(_numpy_function("F", ["x", "u", "dt"], Fm, (11, 11)))
    parts.append("")
    parts.append("")
    parts.append(_numpy_function("H", ["x", "u"], Hm, (3, 11)))
    parts.append("")

    with open(path, "w") as f:
        f.write("\n".join(parts))


def _c_assignments(field_specs, matrix: sp.Matrix, struct_var: str) -> list[str]:
    """Produce CSE'd C assignment lines for a list of (name, row, col)."""
    exprs = [matrix[row, col] for (_n, row, col) in field_specs]
    replacements, reduced = sp.cse(exprs, optimizations="basic")
    lines: list[str] = []
    for sym, sub in replacements:
        lines.append(f"    const float {_printer.doprint(sym)} = {_ccode(sub)};")
    for (name, _row, _col), red in zip(field_specs, reduced):
        lines.append(f"    {struct_var}.{name} = {_ccode(red)};")
    return lines


def emit_c_header(path: str) -> None:
    """Write the core-pure C header of generated F/H entries to ``path``."""
    Fm = F_matrix()
    Hm = H_matrix()
    dd = _build_dd()

    out: list[str] = []
    out.append(_BANNER_C.rstrip("\n"))
    out.append("//")
    out.append("// EKFQ predict Jacobian (F) delta-perturbations and accelerometer")
    out.append("// measurement Jacobian (H) entries, derived symbolically.")
    out.append("//")
    out.append("// F = I + delta_F. Field names match the EKFQ.cpp predict()/correct()")
    out.append("// locals so a later PR can replace the hand-written expressions with")
    out.append("// calls to these fillers mechanically. F is the Jacobian of the LINEAR")
    out.append("// quaternion mean (q + q_dot*dt) by design; the beta row uses the R21")
    out.append("// polynomial form, so every entry below is polynomial -- the header is")
    out.append("// core-pure (no <math.h>, no pow()).")
    out.append("//")
    out.append("// NOT YET INCLUDED BY THE FIRMWARE. A follow-up PR slots it into")
    out.append("// EKFQ.cpp predict()/correct() and deletes the hand expressions.")
    out.append("")
    out.append("#ifndef ONSPEED_CORE_AHRS_EKFQ_JACOBIANS_GENERATED_H")
    out.append("#define ONSPEED_CORE_AHRS_EKFQ_JACOBIANS_GENERATED_H")
    out.append("")
    out.append("namespace onspeed {")
    out.append("")
    # ---- F struct ----
    out.append("// Delta-perturbation entries of the predict Jacobian F = I + delta_F.")
    out.append("// Quaternion block (a01..a32), quaternion-bias block (qb_*),")
    out.append("// vertical channel (vz_*, z_*), and the tas-gated beta row (beta_*).")
    out.append("struct EkfqFJacobian {")
    for (name, _r, _c) in F_FIELDS:
        out.append(f"    float {name};")
    out.append("};")
    out.append("")
    # ---- F filler ----
    out.append("// Fill the F delta-perturbation entries. Inputs are the same locals")
    out.append("// EKFQ.cpp::predict computes: bias-corrected gyros are folded into the")
    out.append("// expressions via (p,q,r) and (bp,bq,br); half_dt = 0.5*dt is implicit")
    out.append("// in the symbolic dt. Pass raw gyros (p,q,r), gyro biases (bp,bq,br),")
    out.append("// quaternion (q0..q3), raw accels (ax_raw,ay_raw,az_raw), tas, and dt.")
    out.append("// Caller is responsible for the tas gate: when tas <= tas_min the beta")
    out.append("// row entries (beta_q0..beta_q3, beta_br_dt) must be treated as zero.")
    out.append("static inline EkfqFJacobian ekfqFJacobian(")
    out.append("        float q0, float q1, float q2, float q3,")
    out.append("        float bp, float bq, float br,")
    out.append("        float p, float q, float r,")
    out.append("        float ax_raw, float ay_raw, float az_raw,")
    out.append("        float tas, float dt) {")
    out.append("    EkfqFJacobian F;")
    out.extend(_c_assignments(F_FIELDS, Fm, "F"))
    out.append("    return F;")
    out.append("}")
    out.append("")
    # ---- a_D partials (documented shared subexpressions) ----
    out.append("// Partials of the earth-down inertial accel a_D w.r.t. the quaternion.")
    out.append("// EKFQ.cpp uses these as named intermediates: vz_q* = dt*dD_q*,")
    out.append("// z_q* = -0.5*dt*dt*dD_q*. Exposed for parity / documentation.")
    out.append("struct EkfqADPartials {")
    for (name, _e) in dd:
        out.append(f"    float {name};")
    out.append("};")
    out.append("static inline EkfqADPartials ekfqADPartials(")
    out.append("        float q0, float q1, float q2, float q3,")
    out.append("        float ax_raw, float ay_raw, float az_raw) {")
    out.append("    EkfqADPartials d;")
    dd_specs = [(name, e) for (name, e) in dd]
    dd_exprs = [e for (_n, e) in dd_specs]
    dd_repl, dd_red = sp.cse(dd_exprs, optimizations="basic")
    for sym, sub in dd_repl:
        out.append(f"    const float {_printer.doprint(sym)} = {_ccode(sub)};")
    for (name, _e), red in zip(dd_specs, dd_red):
        out.append(f"    d.{name} = {_ccode(red)};")
    out.append("    return d;")
    out.append("}")
    out.append("")
    # ---- H struct ----
    out.append("// Non-zero entries of the accelerometer measurement Jacobian H (3x11).")
    out.append("// ax/ay/az rows; quaternion columns plus the centripetal bias couplings")
    out.append("// ay_br = d(ay_pred)/d(br) = -tas and az_bq = d(az_pred)/d(bq) = +tas.")
    out.append("struct EkfqHJacobian {")
    for (name, _r, _c) in H_FIELDS:
        out.append(f"    float {name};")
    out.append("};")
    out.append("")
    out.append("// Fill the H entries. Pass the quaternion (q0..q3) and tas. The accel")
    out.append("// predictions also depend on (q,r,bq,br,tasdot) but H's non-zero entries")
    out.append("// are functions of (q0..q3) and tas only.")
    out.append("static inline EkfqHJacobian ekfqHJacobian(")
    out.append("        float q0, float q1, float q2, float q3, float tas) {")
    out.append("    EkfqHJacobian H;")
    out.extend(_c_assignments(H_FIELDS, Hm, "H"))
    out.append("    return H;")
    out.append("}")
    out.append("")
    out.append("}  // namespace onspeed")
    out.append("")
    out.append("#endif  // ONSPEED_CORE_AHRS_EKFQ_JACOBIANS_GENERATED_H")
    out.append("")

    with open(path, "w") as f:
        f.write("\n".join(out))


# ---------------------------------------------------------------------------
# Paths and entry point
# ---------------------------------------------------------------------------

_HERE = os.path.dirname(os.path.abspath(__file__))
_NUMPY_OUT = os.path.join(_HERE, "_generated_jacobians.py")
_C_HEADER_OUT = os.path.normpath(os.path.join(
    _HERE, "..", "..",
    "software", "Libraries", "onspeed_core", "src", "ahrs",
    "EKFQ_jacobians.generated.h",
))


def main() -> None:
    emit_numpy(_NUMPY_OUT)
    emit_c_header(_C_HEADER_OUT)
    print(f"regenerated:\n  {_NUMPY_OUT}\n  {_C_HEADER_OUT}")


if __name__ == "__main__":
    main()
