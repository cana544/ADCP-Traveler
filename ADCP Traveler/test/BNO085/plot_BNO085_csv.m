% Plot BNO085 accelerometer, linear acceleration, and integrated gyro angle data.
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

    requiredColumns = ["Time_s", "AccelX", "AccelY", "AccelZ", "LinX", "LinY", "LinZ", "AngleX_rad", "AngleY_rad", "AngleZ_rad"];
    missingColumns = setdiff(requiredColumns, string(data.Properties.VariableNames));
    if ~isempty(missingColumns)
        error("Missing columns in %s: %s", csvPath, strjoin(missingColumns, ", "));
    end

    figure("Name", "BNO085 sensor test");
    tiledlayout(3, 1);

    nexttile;
    plot(data.Time_s, data{:, ["AccelX", "AccelY", "AccelZ"]}, "LineWidth", 1);
    title("Accelerometer");
    ylabel("m/s^2");
    legend("X", "Y", "Z", "Location", "best");
    grid on;

    nexttile;
    plot(data.Time_s, data{:, ["LinX", "LinY", "LinZ"]}, "LineWidth", 1);
    title("Linear acceleration");
    ylabel("m/s^2");
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
