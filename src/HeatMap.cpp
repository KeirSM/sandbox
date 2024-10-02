#include "HeatMap.h"
#include <iostream>
#include <opencv2/core.hpp> // Ensure core functionalities are included

namespace HeatMap
{
    void setRectValue(cv::Mat& mat, const cv::Rect& rect, float value)
    {
        // Check if the input matrix is valid
        if (mat.empty())
        {
            throw std::invalid_argument("Input matrix is empty.");
        }

        // Calculate the valid rectangle
        int x1 = std::max(0, rect.x);
        int y1 = std::max(0, rect.y);
        int x2 = std::min(mat.cols, rect.x + rect.width);
        int y2 = std::min(mat.rows, rect.y + rect.height);

        // Create a bounded rectangle
        cv::Rect boundedRect(x1, y1, x2 - x1, y2 - y1);

        // Check if the bounded rectangle is valid
        if (boundedRect.width <= 0 || boundedRect.height <= 0)
        {
            return; // No area to set
        }

        // Set all values within the bounded rectangle to the specified value
        mat(boundedRect).setTo(value);
    }


    void Grid::update(const cv::Mat& kMat)
    {
        // Restart simulation if requested or if size has changed
        const cv::Size kMatSize = kMat.size();
        if (restartRequested || kMatSize != temps.size())
        {
            restartRequested = false;

            if (kMatSize.width <= 0 || kMatSize.height <= 0)
            {
                return;
            }

            // Boundaries are permanently at 0, all other cells are also initialized to 0
            temps = cv::Mat{ kMat.size(), CV_32F, 0.f };

            // Populate initial conditions
            updateSources();
        }

        for (; stepsRequested > 0; stepsRequested--)
        {
            static cv::Mat workingTemps{};
            temps.copyTo(workingTemps);

            // Use OpenCV's parallel_for_ for parallel processing
            cv::Range range(1, temps.rows - 1);
            cv::parallel_for_(range, [&](const cv::Range& r) {
                for (int i = r.start; i < r.end; i++)
                {
                    for (int j = 1; j < temps.cols - 1; j++)
                    {
                        workingTemps.at<float>(i, j) = getNewTemp(i, j, kMat);
                    }
                }
                });

            workingTemps.copyTo(temps);
            updateSources();
        }
    }

    void Grid::updateSources()
    {
        for (auto& source : sources)
        {
            setRectValue(temps, source.area, source.getTempRaw());
        }
    }

    float Grid::getNewTemp(int i, int j, const cv::Mat& kMat)
    {
        switch (algorithm)
        {
        case Algorithms::Average:
        {
            const float sum =
                temps.at<float>(i - 1, j) +
                temps.at<float>(i + 1, j) +
                temps.at<float>(i, j - 1) +
                temps.at<float>(i, j + 1);

            return sum / 4;
        }
        case Algorithms::HeatEquation:
        {
            //constexpr float dx = 1.f;
            //constexpr float dy = 1.f;
            constexpr float dt = 0.25f;
            //constexpr float kMultiplier = 1.0f;

            const float currCell = temps.at<float>(i, j);
            float k = kMat.at<float>(i, j);
            k = k * k * k;

            const float hSum =
                temps.at<float>(i - 1, j) - 2 * currCell + temps.at<float>(i + 1, j);
            const float vSum =
                temps.at<float>(i, j - 1) - 2 * currCell + temps.at<float>(i, j + 1);

            return currCell + dt * k * (hSum + vSum);
        }
        default:
            return temps.at<float>(i, j);
        }
    }
}
