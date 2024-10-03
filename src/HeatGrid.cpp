#include "HeatGrid.h"
#include <iostream>
#include <opencv2/core.hpp> // Ensure core functionalities are included
#include <opencv2/imgproc/imgproc.hpp>
#include <immintrin.h> // For AVX intrinsics
#include <xmmintrin.h> // For SSE intrinsics
#include <omp.h>


void setRectValue(cv::Mat& mat, const cv::Rect& rect, float value)
{
    // Check if the input matrix is valid
    if (mat.empty()) { return; }

    // Calculate the valid rectangle
    int x1 = std::max(0, rect.x);
    int y1 = std::max(0, rect.y);
    int x2 = std::min(mat.cols, rect.x + rect.width);
    int y2 = std::min(mat.rows, rect.y + rect.height);

    // Create a bounded rectangle
    cv::Rect boundedRect(x1, y1, x2 - x1, y2 - y1);

    // Check if the bounded rectangle is valid
    if (boundedRect.width <= 0 || boundedRect.height <= 0) { return; }

    // Set all values within the bounded rectangle to the specified value
    mat(boundedRect).setTo(value);
}

void HeatGrid::update(const cv::Mat& kMat, int iterations)
{
    const cv::Size kMatSize = kMat.size();
    if (kMatSize.width <= 0 || kMatSize.height <= 0) { return; }

    // Boundaries are permanently at 0, all other cells are also initialized to 0
    if (m_temps.size() != kMat.size())
    {
        m_temps = cv::Mat(kMat.size(), CV_32F, 0.f);
    }

    // make sure heat sources are in their place
    updateSources();

    // create the temporary matrix we can work with
    m_temps.copyTo(m_workingTemps);
    m_temps.copyTo(m_result);

    for (int iter = 0; iter < iterations; iter++)
    {
        if (m_algorithm == Algorithms::Average)
        {   
            formulaAvg(kMat);
        }
        else if (m_algorithm == Algorithms::HeatEquation)
        {
            formulaHeatParallel(kMat);
        }
        else if (m_algorithm == Algorithms::HeatEquationKernel)
        {
            formulaHeatKernel(kMat);
        }
        else if (m_algorithm == Algorithms::HeatEquationSIMD)
        {
            formulaHeatSIMD(kMat);
        }
    }

    // normalize the data between a given min and max temperature
    // 0.5 in the normalized data will be equivalent to 0 for visualization
    const double maxVal = 100;       // this will be 1
    const double minVal = -maxVal;   // this will be 0
    m_normalized = (m_temps - minVal) / (maxVal - minVal);
}

void HeatGrid::updateSources()
{
    for (auto& source : m_sources)
    {
        setRectValue(m_temps, source.m_area, source.m_temp);
    }
}

void HeatGrid::formulaAvg(const cv::Mat& kMat)
{
    constexpr float dt = 0.25f;
    cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
        0.00, 0.25, 0.00,
        0.25, 0.00, 0.25,
        0.00, 0.25, 0.00);

    cv::filter2D(m_temps, m_workingTemps, CV_32F, kernel);
    m_workingTemps.copyTo(m_temps);
    updateSources();
}

void HeatGrid::formulaAvgSIMD(const cv::Mat& kMat)
{
    constexpr float dt = 0.25f;

    const int rows = m_temps.rows - 1;
    const int cols = m_temps.cols - 1;

    // Parallelize using OpenCV's parallel_for_ over the rows
    cv::parallel_for_(cv::Range(1, rows), [&](const cv::Range& range)
        {
            for (int i = range.start; i < range.end; ++i)
            {
                // SIMD loop: process 8 elements per iteration
                for (int j = 1; j < cols - 7; j += 8)
                {
                    // Load the current cell and k value
                    __m256 cell = _mm256_loadu_ps(&m_temps.at<float>(i, j));
                    __m256 k = _mm256_loadu_ps(&kMat.at<float>(i, j));
                    k = _mm256_mul_ps(_mm256_mul_ps(k, k), k); // k = k * k * k

                    // Load the neighbors
                    __m256 up = _mm256_loadu_ps(&m_temps.at<float>(i - 1, j));
                    __m256 down = _mm256_loadu_ps(&m_temps.at<float>(i + 1, j));
                    __m256 left = _mm256_loadu_ps(&m_temps.at<float>(i, j - 1));
                    __m256 right = _mm256_loadu_ps(&m_temps.at<float>(i, j + 1));

                    // Calculate the sum of the neighbors
                    __m256 neighborSum = _mm256_add_ps(_mm256_add_ps(up, down), _mm256_add_ps(left, right));

                    // Update the new cell value using the heat equation
                    __m256 newCell = _mm256_div_ps(neighborSum, _mm256_set1_ps(4.0f));

                    // Store the result back to workingTemps
                    _mm256_storeu_ps(&m_workingTemps.at<float>(i, j), newCell);
                }

                // Scalar loop for the remaining elements
                for (int j = cols - (cols % 8); j < cols; ++j)
                {
                    float& cell = m_temps.at<float>(i, j);
                    float& newCell = m_workingTemps.at<float>(i, j);
                    float k = kMat.at<float>(i, j);
                    k = k * k * k;

                    const float neighborSum =
                        m_temps.at<float>(i - 1, j) +
                        m_temps.at<float>(i + 1, j) +
                        m_temps.at<float>(i, j - 1) +
                        m_temps.at<float>(i, j + 1);

                    newCell = neighborSum / 4.0f;
                }
            }
        });

    // Copy the result from workingTemps to temps
    m_workingTemps.copyTo(m_temps);
    updateSources();
}


void HeatGrid::formulaHeat(const cv::Mat& kMat)
{
    constexpr float dt = 0.25f;

    for (int i = 1; i < m_temps.rows - 1; ++i)
    {
        for (int j = 1; j < m_temps.cols - 1; ++j)
        {
            float& cell = m_temps.at<float>(i, j);
            float& newCell = m_workingTemps.at<float>(i, j);
            float k = kMat.at<float>(i, j);
            k = k * k * k;

            // Calculate the sum of the neighbors
            const float neighborSum =
                m_temps.at<float>(i - 1, j) +
                m_temps.at<float>(i + 1, j) +
                m_temps.at<float>(i, j - 1) +
                m_temps.at<float>(i, j + 1);

            // Update the new cell value using the heat equation
            newCell = cell + dt * k * (neighborSum - 4 * cell);
        }
    }

    // Copy the result from workingTemps to temps
    m_workingTemps.copyTo(m_temps);
    updateSources();
}

void HeatGrid::formulaHeatParallel(const cv::Mat& kMat)
{
    cv::Range range(1, m_temps.rows - 1);
    cv::parallel_for_(range, [&](const cv::Range& r) {
        for (int i = r.start; i < r.end; i++)
        {
            for (int j = 1; j < m_temps.cols - 1; j++)
            {
                constexpr float dt = 0.25f;
                float& cell = m_temps.at<float>(i, j);
                float& newCell = m_workingTemps.at<float>(i, j);
                float k = kMat.at<float>(i, j);
                k = k * k * k;

                const float neighBourSum =
                    m_temps.at<float>(i - 1, j) +
                    m_temps.at<float>(i + 1, j) +
                    m_temps.at<float>(i, j - 1) +
                    m_temps.at<float>(i, j + 1);

                newCell = cell + dt * k * (neighBourSum - 4 * cell);
            }
        }
        });

    m_workingTemps.copyTo(m_temps);
    updateSources();
}

void HeatGrid::formulaHeatOMP(const cv::Mat& kMat)
{
    constexpr float dt = 0.25f;

    // Parallelize the outer loop with OpenMP
    #pragma omp parallel for collapse(2) // Collapse 2 loops (i and j) for better load balancing
    for (int i = 1; i < m_temps.rows - 1; ++i)
    {
        for (int j = 1; j < m_temps.cols - 1; ++j)
        {
            float& cell = m_temps.at<float>(i, j);
            float& newCell = m_workingTemps.at<float>(i, j);
            float k = kMat.at<float>(i, j);
            k = k * k * k;

            const float neighborSum =
                m_temps.at<float>(i - 1, j) +
                m_temps.at<float>(i + 1, j) +
                m_temps.at<float>(i, j - 1) +
                m_temps.at<float>(i, j + 1);

            newCell = cell + dt * k * (neighborSum - 4 * cell);
        }
    }

    // Copy the result from workingTemps to temps
    m_workingTemps.copyTo(m_temps);
    updateSources();
}

void HeatGrid::formulaHeatSIMD(const cv::Mat& kMat)
{
    cv::Range range(1, m_temps.rows - 1);
    cv::parallel_for_(range, [&](const cv::Range& r) {
        for (int i = r.start; i < r.end; i++)
        {
            int j = 1;

            // Process 8 floats at a time with AVX (256-bit registers)
            for (; j <= m_temps.cols - 9; j += 8)
            {
                constexpr float dt = 0.25f;

                // Load the current cell values into an AVX register
                __m256 cell = _mm256_loadu_ps(&m_temps.at<float>(i, j));

                // Load the neighboring cell values into AVX registers
                __m256 north = _mm256_loadu_ps(&m_temps.at<float>(i - 1, j));
                __m256 south = _mm256_loadu_ps(&m_temps.at<float>(i + 1, j));
                __m256 west  = _mm256_loadu_ps(&m_temps.at<float>(i, j - 1));
                __m256 east  = _mm256_loadu_ps(&m_temps.at<float>(i, j + 1));

                // Compute the neighbor sum: north + south + west + east
                __m256 neighborSum = _mm256_add_ps(north, south);
                neighborSum = _mm256_add_ps(neighborSum, west);
                neighborSum = _mm256_add_ps(neighborSum, east);

                // Load the k values and cube them
                __m256 k = _mm256_loadu_ps(&kMat.at<float>(i, j));
                k        = _mm256_mul_ps(k, _mm256_mul_ps(k, k)); // k = k^3

                // Compute the heat equation: newCell = cell + dt * k * (neighborSum - 4 * cell)
                __m256 fourCell = _mm256_mul_ps(cell, _mm256_set1_ps(4.0f));
                __m256 diff     = _mm256_sub_ps(neighborSum, fourCell);
                __m256 dtK      = _mm256_mul_ps(_mm256_set1_ps(dt), k);
                __m256 newCell  = _mm256_add_ps(cell, _mm256_mul_ps(dtK, diff));

                // Store the result back into the workingTemps matrix
                _mm256_storeu_ps(&m_workingTemps.at<float>(i, j), newCell);
            }

            // Handle the remaining columns that are not divisible by 8
            for (; j < m_temps.cols - 1; j++)
            {
                constexpr float dt = 0.25f;
                float& cell = m_temps.at<float>(i, j);
                float& newCell = m_workingTemps.at<float>(i, j);
                float k = kMat.at<float>(i, j);
                k = k * k * k;

                const float neighborSum =
                    m_temps.at<float>(i - 1, j) +
                    m_temps.at<float>(i + 1, j) +
                    m_temps.at<float>(i, j - 1) +
                    m_temps.at<float>(i, j + 1);

                newCell = cell + dt * k * (neighborSum - 4 * cell);
            }
        }
        });

    m_workingTemps.copyTo(m_temps);
    updateSources();
}

void HeatGrid::formulaHeatSIMD128(const cv::Mat& kMat)
{
    constexpr float dt = 0.25f;
    const int simdWidth = 4; // SSE processes 4 floats at a time (128-bit)

    cv::Range range(1, m_temps.rows - 1);
    cv::parallel_for_(range, [&](const cv::Range& r) {
        for (int i = r.start; i < r.end; ++i)
        {
            int j = 1;

            // Process columns in chunks of 4 (SSE width)
            for (; j <= m_temps.cols - simdWidth - 1; j += simdWidth)
            {
                // Load current cell values
                __m128 cell = _mm_loadu_ps(&m_temps.at<float>(i, j));

                // Load neighboring values
                __m128 north = _mm_loadu_ps(&m_temps.at<float>(i - 1, j));
                __m128 south = _mm_loadu_ps(&m_temps.at<float>(i + 1, j));
                __m128 west = _mm_loadu_ps(&m_temps.at<float>(i, j - 1));
                __m128 east = _mm_loadu_ps(&m_temps.at<float>(i, j + 1));

                // Sum neighboring values: north + south + west + east
                __m128 neighborSum = _mm_add_ps(north, south);
                neighborSum = _mm_add_ps(neighborSum, west);
                neighborSum = _mm_add_ps(neighborSum, east);

                // Load k values and cube them: k = k^3
                __m128 k = _mm_loadu_ps(&kMat.at<float>(i, j));
                k = _mm_mul_ps(k, _mm_mul_ps(k, k)); // k = k^3

                // Compute: newCell = cell + dt * k * (neighborSum - 4 * cell)
                __m128 fourCell = _mm_mul_ps(cell, _mm_set1_ps(4.0f));
                __m128 diff = _mm_sub_ps(neighborSum, fourCell);
                __m128 dtK = _mm_mul_ps(_mm_set1_ps(dt), k);
                __m128 newCell = _mm_add_ps(cell, _mm_mul_ps(dtK, diff));

                // Store result back into m_workingTemps
                _mm_storeu_ps(&m_workingTemps.at<float>(i, j), newCell);
            }

            // Handle remaining elements that are not divisible by 4
            for (; j < m_temps.cols - 1; ++j)
            {
                float& cell = m_temps.at<float>(i, j);
                float& newCell = m_workingTemps.at<float>(i, j);
                float k = kMat.at<float>(i, j);
                k = k * k * k;

                const float neighborSum =
                    m_temps.at<float>(i - 1, j) +
                    m_temps.at<float>(i + 1, j) +
                    m_temps.at<float>(i, j - 1) +
                    m_temps.at<float>(i, j + 1);

                newCell = cell + dt * k * (neighborSum - 4 * cell);
            }
        }
        });

    // Copy results from m_workingTemps back to m_temps
    m_workingTemps.copyTo(m_temps);
    updateSources();
}


void ParallelAdd(const cv::Mat& mat1, const cv::Mat& mat2, cv::Mat& result)
{
    cv::parallel_for_(cv::Range(0, mat1.rows), [&](const cv::Range& range)
        {
            for (int i = range.start; i < range.end; i++)
            {
                for (int j = 0; j < mat1.cols; j++)
                {
                    result.at<float>(i, j) = mat1.at<float>(i, j) + mat2.at<float>(i, j);
                }
            }
        });
}

void ParallelMultiply(const cv::Mat& mat1, const cv::Mat& mat2, cv::Mat& result)
{
    cv::parallel_for_(cv::Range(0, mat1.rows), [&](const cv::Range& range)
        {
            for (int i = range.start; i < range.end; i++)
            {
                for (int j = 0; j < mat1.cols; j++)
                {
                    result.at<float>(i, j) = mat1.at<float>(i, j) * mat2.at<float>(i, j);
                }
            }
        });
}

void HeatGrid::formulaHeatKernel(const cv::Mat& kMat)
{
    constexpr float dt = 0.25f;
    cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
        0.00, dt, 0.00,
        dt, dt*-4.00, dt,
        0.00, dt, 0.00);

    cv::filter2D(m_temps, m_workingTemps, CV_32F, kernel);

    cv::multiply(m_workingTemps, kMat, m_result);
    cv::add(m_temps, m_result, m_workingTemps);

    m_workingTemps.copyTo(m_temps);
    updateSources();
}
