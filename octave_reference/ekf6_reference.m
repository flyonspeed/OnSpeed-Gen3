% EKF6 Reference Implementation for OnSpeed
% Simplified and numerically stable version for C++ validation
%
% State vector: [phi, theta, alpha, bp, bq, br]
%   phi   - roll angle (rad)
%   theta - pitch angle (rad)
%   alpha - angle of attack (rad)
%   bp    - roll rate gyro bias (rad/s)
%   bq    - pitch rate gyro bias (rad/s)
%   br    - yaw rate gyro bias (rad/s)
%
% Measurements: [ax, ay, az, alpha_meas]
%   ax, ay, az  - accelerometer (m/s^2)
%   alpha_meas  - derived alpha from theta - gamma (rad)
%
% Usage: octave --no-gui ekf6_reference.m

function ekf6_reference()
    % Configuration matching Gen3 hardware
    dt = 1/208;
    duration = 5.0;
    n_samples = round(duration / dt);

    g = 9.80665;
    deg2rad = pi/180;
    rad2deg = 180/pi;

    fprintf('EKF6 Reference Implementation\n');
    fprintf('dt = %.6f s (%.1f Hz), duration = %.1f s\n\n', dt, 1/dt, duration);

    % Run tests
    test_level_flight(dt, n_samples, g, deg2rad, rad2deg);
    test_pitched_attitude(dt, n_samples, g, deg2rad, rad2deg, 10);  % 10 deg pitch
    test_banked_attitude(dt, n_samples, g, deg2rad, rad2deg, 20);   % 20 deg bank
    test_pitch_rate(dt, n_samples, g, deg2rad, rad2deg, 5, 2.0);    % 5 deg/s for 2s
    test_gyro_bias(dt, n_samples, g, deg2rad, rad2deg, 2);          % 2 deg/s bias

    fprintf('\nAll CSV files saved.\n');
end

% ============================================================================
% EKF Core Implementation
% ============================================================================

function [x, P] = ekf_init()
    % Initialize state and covariance
    x = zeros(6, 1);

    % Initial covariance - moderate uncertainty
    P = diag([0.1, 0.1, 0.1, 0.01, 0.01, 0.01]);
end

function [Q, R] = ekf_get_noise_matrices()
    % Process noise - how much state can change between updates
    q_attitude = 0.001;      % Attitude process noise (rad^2)
    q_alpha = 0.0001;        % Alpha process noise
    q_bias = 1e-8;           % Gyro bias drift (very slow)

    Q = diag([q_attitude, q_attitude, q_alpha, q_bias, q_bias, q_bias]);

    % Measurement noise
    r_accel = 0.5;           % Accelerometer noise (m/s^2)^2
    r_alpha = 0.01;          % Alpha measurement noise (rad^2)

    R = diag([r_accel, r_accel, r_accel, r_alpha]);
end

function [x_new, P_new] = ekf_predict(x, P, Q, dt, p, q, r)
    % Prediction step using Euler angle kinematics
    %
    % phi_dot   = p - bp + (q - bq)*sin(phi)*tan(theta) + (r - br)*cos(phi)*tan(theta)
    % theta_dot = (q - bq)*cos(phi) - (r - br)*sin(phi)
    % alpha_dot = 0 (assumed constant between measurements)
    % bias_dot  = 0 (biases change very slowly)

    phi = x(1);
    theta = x(2);
    bp = x(4);
    bq = x(5);
    br = x(6);

    % Bias-corrected rates
    p_corr = p - bp;
    q_corr = q - bq;
    r_corr = r - br;

    % Trig terms with singularity protection
    sph = sin(phi);
    cph = cos(phi);
    cth = cos(theta);
    if abs(cth) < 0.001
        cth = sign(cth) * 0.001;  % Protect against tan(90)
    end
    tth = sin(theta) / cth;

    % State derivatives
    phi_dot = p_corr + q_corr * sph * tth + r_corr * cph * tth;
    theta_dot = q_corr * cph - r_corr * sph;

    % Predict state
    x_new = x;
    x_new(1) = x(1) + dt * phi_dot;
    x_new(2) = x(2) + dt * theta_dot;
    % States 3-6 unchanged (alpha and biases are constant)

    % State transition Jacobian
    % Row 1: d(phi_dot)/d[phi, theta, alpha, bp, bq, br]
    % Row 2: d(theta_dot)/d[phi, theta, alpha, bp, bq, br]

    F = eye(6);

    % d(phi_dot)/d(phi)
    F(1,1) = 1 + dt * (q_corr * cph * tth - r_corr * sph * tth);

    % d(phi_dot)/d(theta) - use sec^2(theta) = 1 + tan^2(theta)
    sec2th = 1 + tth*tth;
    F(1,2) = dt * (q_corr * sph * sec2th + r_corr * cph * sec2th);

    % d(phi_dot)/d(bp)
    F(1,4) = -dt;

    % d(phi_dot)/d(bq)
    F(1,5) = -dt * sph * tth;

    % d(phi_dot)/d(br)
    F(1,6) = -dt * cph * tth;

    % d(theta_dot)/d(phi)
    F(2,1) = dt * (-q_corr * sph - r_corr * cph);

    % d(theta_dot)/d(theta) = 0, so F(2,2) = 1

    % d(theta_dot)/d(bq)
    F(2,5) = -dt * cph;

    % d(theta_dot)/d(br)
    F(2,6) = dt * sph;

    % Predict covariance
    P_new = F * P * F' + Q;
end

function [x_new, P_new] = ekf_update(x, P, R, g, Va, gamma, ax, ay, az)
    % Update step using accelerometer measurements

    phi = x(1);
    theta = x(2);
    alpha = x(3);
    bp = x(4);
    bq = x(5);
    br = x(6);

    sph = sin(phi);
    cph = cos(phi);
    sth = sin(theta);
    cth = cos(theta);

    % Measurement prediction (accelerometer in body frame due to gravity)
    % ax_pred = g * sin(theta)
    % ay_pred = -g * cos(theta) * sin(phi)
    % az_pred = -g * cos(theta) * cos(phi)
    % alpha_pred = alpha (identity)

    z_pred = [g * sth; ...
              -g * cth * sph; ...
              -g * cth * cph; ...
              alpha];

    % Measurement Jacobian
    H = zeros(4, 6);

    % d(ax)/d(theta)
    H(1, 2) = g * cth;

    % d(ay)/d(phi), d(ay)/d(theta)
    H(2, 1) = -g * cth * cph;
    H(2, 2) = g * sth * sph;

    % d(az)/d(phi), d(az)/d(theta)
    H(3, 1) = g * cth * sph;
    H(3, 2) = g * sth * cph;

    % d(alpha)/d(alpha)
    H(4, 3) = 1;

    % Actual measurements
    % Alpha measurement comes from pitch - flight path angle
    alpha_meas = theta - gamma;
    z = [ax; ay; az; alpha_meas];

    % Innovation
    y = z - z_pred;

    % Innovation covariance
    S = H * P * H' + R;

    % Kalman gain
    K = P * H' / S;

    % Update state
    x_new = x + K * y;

    % Update covariance (Joseph form for numerical stability)
    I_KH = eye(6) - K * H;
    P_new = I_KH * P * I_KH' + K * R * K';
end

% ============================================================================
% Test Cases
% ============================================================================

function test_level_flight(dt, n_samples, g, deg2rad, rad2deg)
    fprintf('=== Test: Level Flight ===\n');

    [x, P] = ekf_init();
    [Q, R] = ekf_get_noise_matrices();

    Va = 50;    % m/s
    gamma = 0;  % Level flight

    % True values (level)
    true_ax = 0;
    true_ay = 0;
    true_az = -g;  % Gravity acts downward

    results = zeros(n_samples, 7);

    for n = 1:n_samples
        t = (n-1) * dt;

        % Gyro readings (no rotation)
        p = 0; q = 0; r = 0;

        % Predict
        [x, P] = ekf_predict(x, P, Q, dt, p, q, r);

        % Update with accelerometer
        [x, P] = ekf_update(x, P, R, g, Va, gamma, true_ax, true_ay, true_az);

        results(n, :) = [t, x(1)*rad2deg, x(2)*rad2deg, x(3)*rad2deg, ...
                         x(4)*rad2deg, x(5)*rad2deg, x(6)*rad2deg];
    end

    save_results('level_flight', results);
    fprintf('  Final: phi=%.3f, theta=%.3f, alpha=%.3f deg\n', ...
            results(end,2), results(end,3), results(end,4));
end

function test_pitched_attitude(dt, n_samples, g, deg2rad, rad2deg, pitch_deg)
    fprintf('=== Test: %.0f deg Pitch ===\n', pitch_deg);

    [x, P] = ekf_init();
    [Q, R] = ekf_get_noise_matrices();

    Va = 50;
    gamma = 0;  % Simplified: level flight path
    theta_true = pitch_deg * deg2rad;

    % Accelerometer in pitched attitude
    true_ax = g * sin(theta_true);
    true_ay = 0;
    true_az = -g * cos(theta_true);

    results = zeros(n_samples, 7);

    for n = 1:n_samples
        t = (n-1) * dt;

        p = 0; q = 0; r = 0;

        [x, P] = ekf_predict(x, P, Q, dt, p, q, r);
        [x, P] = ekf_update(x, P, R, g, Va, gamma, true_ax, true_ay, true_az);

        results(n, :) = [t, x(1)*rad2deg, x(2)*rad2deg, x(3)*rad2deg, ...
                         x(4)*rad2deg, x(5)*rad2deg, x(6)*rad2deg];
    end

    save_results('pitched_10deg', results);
    fprintf('  Final: phi=%.3f, theta=%.3f (expect %.1f), alpha=%.3f deg\n', ...
            results(end,2), results(end,3), pitch_deg, results(end,4));
end

function test_banked_attitude(dt, n_samples, g, deg2rad, rad2deg, bank_deg)
    fprintf('=== Test: %.0f deg Bank ===\n', bank_deg);

    [x, P] = ekf_init();
    [Q, R] = ekf_get_noise_matrices();

    Va = 50;
    gamma = 0;
    phi_true = bank_deg * deg2rad;

    % Accelerometer in banked attitude
    true_ax = 0;
    true_ay = -g * sin(phi_true);
    true_az = -g * cos(phi_true);

    results = zeros(n_samples, 7);

    for n = 1:n_samples
        t = (n-1) * dt;

        p = 0; q = 0; r = 0;

        [x, P] = ekf_predict(x, P, Q, dt, p, q, r);
        [x, P] = ekf_update(x, P, R, g, Va, gamma, true_ax, true_ay, true_az);

        results(n, :) = [t, x(1)*rad2deg, x(2)*rad2deg, x(3)*rad2deg, ...
                         x(4)*rad2deg, x(5)*rad2deg, x(6)*rad2deg];
    end

    save_results('banked_20deg', results);
    fprintf('  Final: phi=%.3f (expect %.1f), theta=%.3f, alpha=%.3f deg\n', ...
            results(end,2), bank_deg, results(end,3), results(end,4));
end

function test_pitch_rate(dt, n_samples, g, deg2rad, rad2deg, rate_dps, dur_s)
    fprintf('=== Test: Pitch Rate %.0f deg/s for %.1f s ===\n', rate_dps, dur_s);

    [x, P] = ekf_init();
    [Q, R] = ekf_get_noise_matrices();

    Va = 50;
    gamma = 0;
    q_rate = rate_dps * deg2rad;  % rad/s

    theta_true = 0;  % Track true pitch
    results = zeros(n_samples, 8);

    for n = 1:n_samples
        t = (n-1) * dt;

        % Apply pitch rate for specified duration
        if t < dur_s
            q = q_rate;
            theta_true = theta_true + q_rate * dt;
        else
            q = 0;
        end

        p = 0; r = 0;

        % True accelerometer
        true_ax = g * sin(theta_true);
        true_ay = 0;
        true_az = -g * cos(theta_true);

        [x, P] = ekf_predict(x, P, Q, dt, p, q, r);
        [x, P] = ekf_update(x, P, R, g, Va, gamma, true_ax, true_ay, true_az);

        results(n, :) = [t, x(1)*rad2deg, x(2)*rad2deg, x(3)*rad2deg, ...
                         x(4)*rad2deg, x(5)*rad2deg, x(6)*rad2deg, theta_true*rad2deg];
    end

    save_results('pitch_rate', results);
    expected_theta = rate_dps * dur_s;
    fprintf('  Final: theta=%.3f (expect %.1f), true=%.3f deg\n', ...
            results(end,3), expected_theta, results(end,8));
end

function test_gyro_bias(dt, n_samples, g, deg2rad, rad2deg, bias_dps)
    fprintf('=== Test: Gyro Bias %.0f deg/s ===\n', bias_dps);

    [x, P] = ekf_init();
    [Q, R] = ekf_get_noise_matrices();

    Va = 50;
    gamma = 0;
    q_bias = bias_dps * deg2rad;  % True gyro bias

    % Aircraft is level, but gyro reports rotation
    true_ax = 0;
    true_ay = 0;
    true_az = -g;

    results = zeros(n_samples, 7);

    for n = 1:n_samples
        t = (n-1) * dt;

        % Gyro reads bias even though aircraft is level
        p = 0;
        q = q_bias;  % Biased reading
        r = 0;

        [x, P] = ekf_predict(x, P, Q, dt, p, q, r);
        [x, P] = ekf_update(x, P, R, g, Va, gamma, true_ax, true_ay, true_az);

        results(n, :) = [t, x(1)*rad2deg, x(2)*rad2deg, x(3)*rad2deg, ...
                         x(4)*rad2deg, x(5)*rad2deg, x(6)*rad2deg];
    end

    save_results('gyro_bias', results);
    fprintf('  Final: theta=%.3f (expect ~0), bq=%.3f (expect %.1f) deg/s\n', ...
            results(end,3), results(end,6), bias_dps);
end

function save_results(name, results)
    filename = sprintf('ekf6_%s.csv', name);
    fid = fopen(filename, 'w');

    if size(results, 2) == 7
        fprintf(fid, 'time,phi_deg,theta_deg,alpha_deg,bp_dps,bq_dps,br_dps\n');
        for n = 1:size(results, 1)
            fprintf(fid, '%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n', ...
                results(n,1), results(n,2), results(n,3), results(n,4), ...
                results(n,5), results(n,6), results(n,7));
        end
    else
        fprintf(fid, 'time,phi_deg,theta_deg,alpha_deg,bp_dps,bq_dps,br_dps,true_theta_deg\n');
        for n = 1:size(results, 1)
            fprintf(fid, '%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n', ...
                results(n,1), results(n,2), results(n,3), results(n,4), ...
                results(n,5), results(n,6), results(n,7), results(n,8));
        end
    end

    fclose(fid);
    fprintf('  -> %s\n', filename);
end

% Run
ekf6_reference();
