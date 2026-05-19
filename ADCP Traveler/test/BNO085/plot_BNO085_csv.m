% Plot BNO085 CSV data exactly as recorded by the firmware.
%
% Save the Arduino Serial Monitor output into BNO085_data.csv, then run:
%   plot_BNO085_csv
%
% Or pass a different file:
%   plot_BNO085_csv("my_capture.csv")

function plot_BNO085_csv(csvPath)
    if nargin < 1
        csvPath = "BNO085_data.csv";
    end

    opts = detectImportOptions(csvPath, "Delimiter", {",", "\t"});
    data = readtable(csvPath, opts);

    requiredColumns = [
        "Time_s", ...
        "RawAccelX", "RawAccelY", "RawAccelZ", ...
        "AccelX", "AccelY", "AccelZ", ...
        "LinX", "LinY", "LinZ", ...
        "VelX", "VelY", "VelZ", ...
        "DispX", "DispY", "DispZ", ...
        "GyroX_rad_s", "GyroY_rad_s", "GyroZ_rad_s", ...
        "AngleX_rad", "AngleY_rad", "AngleZ_rad"
    ];
    missingColumns = setdiff(requiredColumns, string(data.Properties.VariableNames));
    if ~isempty(missingColumns)
        error("Missing columns in %s: %s", csvPath, strjoin(missingColumns, ", "));
    end

    figure("Name", "BNO085 accelerometer");
    tiledlayout(2, 1);

    nexttile;
    plot(data.Time_s, data{:, ["RawAccelX", "RawAccelY", "RawAccelZ"]}, "LineWidth", 1);
    title("Raw accelerometer");
    ylabel("m/s^2");
    legend("X", "Y", "Z", "Location", "best");
    grid on;

    nexttile;
    plot(data.Time_s, data{:, ["AccelX", "AccelY", "AccelZ"]}, "LineWidth", 1);
    title("Signal-conditioned accelerometer");
    xlabel("Time (s)");
    ylabel("m/s^2");
    legend("X", "Y", "Z", "Location", "best");
    grid on;

    figure("Name", "BNO085 motion");
    tiledlayout(3, 1);

    nexttile;
    plot(data.Time_s, data{:, ["LinX", "LinY", "LinZ"]}, "LineWidth", 1);
    title("Firmware Kalman-filtered linear acceleration");
    ylabel("m/s^2");
    legend("X", "Y", "Z", "Location", "best");
    grid on;

    nexttile;
    plot(data.Time_s, data{:, ["VelX", "VelY", "VelZ"]}, "LineWidth", 1);
    title("Velocity");
    ylabel("m/s");
    legend("X", "Y", "Z", "Location", "best");
    grid on;

    nexttile;
    plot(data.Time_s, data{:, ["DispX", "DispY", "DispZ"]}, "LineWidth", 1);
    title("Displacement");
    xlabel("Time (s)");
    ylabel("m");
    legend("X", "Y", "Z", "Location", "best");
    grid on;

    figure("Name", "BNO085 rotation");
    tiledlayout(2, 1);

    nexttile;
    plot(data.Time_s, data{:, ["GyroX_rad_s", "GyroY_rad_s", "GyroZ_rad_s"]}, "LineWidth", 1);
    title("Firmware Kalman-filtered gyroscope");
    ylabel("rad/s");
    legend("X", "Y", "Z", "Location", "best");
    grid on;

    nexttile;
    plot(data.Time_s, data{:, ["AngleX_rad", "AngleY_rad", "AngleZ_rad"]}, "LineWidth", 1);
    title("Integrated gyro angle");
    xlabel("Time (s)");
    ylabel("rad");
    legend("X", "Y", "Z", "Location", "best");
    grid on;
end
