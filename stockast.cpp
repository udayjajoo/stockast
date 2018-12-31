/**
* @file This file is part of stockast.
*
* @section LICENSE
* MIT License
*
* Copyright (c) 2017-18 Rajdeep Konwar
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* @section DESCRIPTION
* Stock Market Forecasting using parallel Monte-Carlo simulations
* (src:wikipedia) The Black–Scholes model assumes that the market consists of
* at least one risky asset, usually called the stock, and one riskless asset,
* usually called the money market, cash, or bond. The rate of return on the
* riskless asset is constant and thus called the risk-free interest rate.
**/

//! Header files
#include <iostream>
#include <cstring>
#include <chrono>
#include <omp.h>
#include <random>

#include "gnuplot-iostream.h"

#define LINE_LEN 4096

//! ---------------------------------------------------------------------------
//! Calculates volatility from ml_data.csv file
//! ---------------------------------------------------------------------------
float calcVolatility(const float& spotPrice, const int& timesteps)
{
	//! Open ml_data.csv in read-mode, exit on fail
	FILE* fp = std::fopen("ml_data.csv", "r");
	if (!fp)
	{
		std::cerr << "Cannot open ml_data.csv! Exiting..\n";
		exit(EXIT_FAILURE);
	}

	char  line[LINE_LEN];
	//! Read the first line then close file
	if (fgets(line, sizeof(line), fp) == nullptr)
	{
		std::cerr << "Cannot read from ml_data.csv! Exiting..\n";
		fclose(fp);
		exit(EXIT_FAILURE);
	}
	fclose(fp);

	char* token;
	int len = timesteps - 1;
	float priceArr[len];
	//! Get the return values of stock from file (min 2 to 180)
	for (int i = 0; i < len; i++)
	{
		if (i == 0)
			token = strtok(line, ",");
		else
			token = strtok(NULL, ",");
		priceArr[i] = std::atof(token);
	}

	float sum = spotPrice;
	//! Find mean of the estimated minute-end prices
	for (int i = 0; i < len; i++)
		sum += priceArr[i];
	float meanPrice = sum / (len + 1);

	//! Calculate market volatility as standard deviation
	sum = powf((spotPrice - meanPrice), 2.0f);
	for (int i = 0; i < len; i++)
		sum += powf((priceArr[i] - meanPrice), 2.0f);
	float stdDev = sqrtf(sum);

	//! Return as percentage
	return (stdDev / 100.0f);
}

/** ---------------------------------------------------------------------------
Finds mean of a 2D array across first index (inLoops)
M is in/outLoops and N is timesteps
----------------------------------------------------------------------------*/
float* find2DMean(float** matrix, const int& numLoops, const int& timesteps)
{
	int j;
	float* avg = new float[timesteps];
	float sum = 0.0f;

	for (int i = 0; i < timesteps; i++)
	{
		/**
		A private copy of 'sum' variable is created for each thread.
		At the end of the reduction, the reduction variable is applied to
		all private copies of the shared variable, and the final result
		is written to the global shared variable.
		*/
#pragma omp parallel for private(j) reduction(+:sum)
		for (j = 0; j < numLoops; j++)
		{
			sum += matrix[j][i];
		}

		//! Calculating average across columns
		avg[i] = sum / numLoops;
		sum = 0.0f;
	}

	return avg;
}

/** ---------------------------------------------------------------------------
Generates a random number seeded by system clock based on standard
normal distribution on taking mean 0.0 and standard deviation 1.0
----------------------------------------------------------------------------*/
float randGen(const float& mean, const float& stdDev)
{
	unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
	std::default_random_engine generator(seed);
	std::normal_distribution<float> distribution(mean, stdDev);
	return distribution(generator);
}

//! ---------------------------------------------------------------------------
//! Simulates Black Scholes model
//! ---------------------------------------------------------------------------
float* runBlackScholesModel(const float& spotPrice, const int& timesteps, const float& riskRate, const float& volatility)
{
	float  mean = 0.0f, stdDev = 1.0f;			//! Mean and standard deviation
	float  deltaT = 1.0f / timesteps;			//! Timestep
	float* normRand = new float[timesteps - 1];	//! Array of normally distributed random nos.
	float* stockPrice = new float[timesteps];	//! Array of stock price at diff. times
	stockPrice[0] = spotPrice;					//! Stock price at t=0 is spot price

	//! Populate array with random nos.
	for (int i = 0; i < n - 1; i++)
		normRand[i] = randGen(mean, stdDev);

	//! Apply Black Scholes equation to calculate stock price at next timestep
	for (int i = 0; i < n - 1; i++)
		stockPrice[i + 1] = stockPrice[i] * exp(((riskRate - (powf(volatility, 2.0f) / 2.0f)) * deltaT) + (volatility * normRand[i] * sqrtf(deltaT)));

	delete[] normRand;
	return stockPrice;
}

//! ---------------------------------------------------------------------------
//! Main function
//! ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
	clock_t t = clock();
	int plotFlag;	//! Flag indicating whether to plot or not

	//! Check for input terminal arguments. If none specified, plot by default
	if (argc != 2)
		plotFlag = 1;
	else
		plotFlag = std::atoi(argv[1]);

	int inLoops = 100;		//! Inner loop iterations
	int outLoops = 10000;	//! Outer loop iterations
	int timesteps = 180;	//! Stock market time-intervals (min)

	//! Matrix for stock-price vectors per iteration
	float** stock = new float *[inLoops];
	for (i = 0; i < inLoops; i++)
		stock[i] = new float[timesteps];

	//! Matrix for mean of stock-price vectors per iteration
	float** avgStock = new float*[outLoops];
	for (i = 0; i < outLoops; i++)
		avgStock[i] = new float[timesteps];

	//! Vector for most likely outcome stock price
	float* optStock = new float[timesteps];

	float riskRate = 0.001f;	//! Risk free interest rate (%)
	float spotPrice = 100.0f;	//! Spot price (at t = 0)

	//! Market volatility (calculated from ml_data.csv)
	float volatility = calcVolatility(spotPrice, timesteps);

	//! Welcome message
	std::cout << "--Welcome to Stockast: Stock Forecasting Tool--\n";
	std::cout << "  Parth Shah, Premanand Kumar, Rajdeep Konwar  \n\n";
	std::cout << "  Using market volatility = " << volatility << std::endl;

	int i;
	//! Parallel region with each thread having its own instance of variable 'i',
#pragma omp parallel private(i)
	{
		//! Only one thread (irrespective of thread id) handles this region
#pragma omp single
		{
			int numThreads = omp_get_num_threads();	//! Number of threads
			std::cout << "  Using " << numThreads << " thread(s)..\n";
			omp_set_num_threads(numThreads);
		}

		/**
		Parallel for loop with dynamic scheduling, i.e. each thread
		grabs "chunk" iterations until all iterations are done.
		Faster threads are assigned more iterations (not Round Robin)
		*/
#pragma omp for schedule(dynamic)
		for (i = 0; i < outLoops; i++)
		{
			/**
			Using Black Scholes model to get stock price every iteration
			Returns data as a column vector having rows=timesteps
			*/
			for (int j = 0; j < inLoops; j++)
				stock[j] = runBlackScholesModel(spotPrice, timesteps, riskRate, volatility);

			//! Stores average of all estimated stock-price arrays
			avgStock[i] = find2DMean(stock, inLoops, timesteps);
		}
		//! --> Implicit omp barrier <--
	}

	//! Average of all the average arrays
	optStock = find2DMean(avgStock, outLoops, timesteps);

	//! Write optimal outcome to disk
	FILE* fp = std::fopen("opt.csv", "w");
	if (!fp)
	{
		std::cerr << "Couldn't open opt.csv! Exiting..\n";
		return EXIT_FAILURE;
	}

	for (i = 0; i < timesteps; i++)
		fprintf(fp, "%f\n", optStock[i]);
	fclose(fp);

	for (i = 0; i < inLoops; i++)
		delete[] stock[i];
	delete[] stock;

	for (i = 0; i < outLoops; i++)
		delete[] avgStock[i];
	delete[] avgStock;

	delete[] optStock;

	//! Plot the most likely outcome in Gnuplot if plotFlag = 1
	if (plotFlag)
	{
		Gnuplot gp("gnuplot -persist");
		gp << "set grid\n";
		gp << "set title 'Stock Market Forecast (3 hrs)'\n";
		gp << "set xlabel 'Time (min)'\n";
		gp << "set ylabel 'Price'\n";
		gp << "plot 'opt.csv' w l title 'Predicted Stock Price'\n";
	}

	t = clock() - t;
	std::cout << "  Time taken = " << static_cast<float>(t / CLOCKS_PER_SEC) << "s\n\n";

	return EXIT_SUCCESS;
}
