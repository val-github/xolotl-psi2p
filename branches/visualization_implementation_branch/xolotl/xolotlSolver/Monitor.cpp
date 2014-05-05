// Includes
#include "PetscSolver.h"
#include "../xolotlPerf/HandlerRegistryFactory.h"
#include "../xolotlViz/VizHandlerRegistryFactory.h"
#include <PlotType.h>
#include <CvsXDataProvider.h>
#include <CvsXYDataProvider.h>
#include <LabelProvider.h>
#include <petscts.h>
#include <petscsys.h>
#include <sstream>
#include <vector>
#include <memory>
#include <HDF5Utils.h>

namespace xolotlSolver {

/* ----- Error Handling Code ----- */

/**
 * This operation checks a Petsc error code and converts it to a bool.
 * @param errorCode The Petsc error code.
 * @return True if everything is OK, false otherwise.
 */
static inline bool checkPetscError(PetscErrorCode errorCode) {
	CHKERRQ(errorCode);
}

//! The xolotlViz handler registry
auto vizHandlerRegistry = xolotlViz::getVizHandlerRegistry();

//! The pointer to the plot that will be used to visualize the data.
std::shared_ptr<xolotlViz::IPlot> plot;

//! The pointer to the series plot that will be used to visualize the data.
std::shared_ptr<xolotlViz::IPlot> seriesPlot;

//! The pointer to the 2D plot that will be used to visualize the data.
std::shared_ptr<xolotlViz::IPlot> surfacePlot;

//! The pointer to the plot that will be used to visualize performance data.
std::shared_ptr<xolotlViz::IPlot> perfPlot;

//! The double that will store the accumulation of helium flux.
double heliumFluence = 0.0;

/**
 * This is a monitoring method that will save an hdf5 file at each time step
 */
static PetscErrorCode startStop(TS ts, PetscInt timestep, PetscReal time,
		Vec solution, void *ictx) {
	// Network size
	const int networkSize = PetscSolver::getNetwork()->size();
	PetscErrorCode ierr;
	PetscReal *solutionArray, *gridPointSolution;
	Vec localSolution;
	PetscInt xs, xm, Mx;

	PetscFunctionBeginUser;

	// Get the number of processes
	int worldSize;
	MPI_Comm_size(PETSC_COMM_WORLD, &worldSize);

	// Gets the process ID (important when it is running in parallel)
	int procId;
	MPI_Comm_rank(MPI_COMM_WORLD, &procId);

	// Get the da from ts
	DM da;
	ierr = TSGetDM(ts, &da);
	checkPetscError(ierr);

	// Get the local vector, which is capital when running in parallel,
	// and put it into solutionArray
	ierr = DMGetLocalVector(da, &localSolution);
	checkPetscError(ierr);
	ierr = DMGlobalToLocalBegin(da, solution, INSERT_VALUES, localSolution);
	checkPetscError(ierr);
	ierr = DMGlobalToLocalEnd(da, solution, INSERT_VALUES, localSolution);
	checkPetscError(ierr);
	ierr = DMDAVecGetArray(da, localSolution, &solutionArray);
	checkPetscError(ierr);

	// Get the corners of the grid
	ierr = DMDAGetCorners(da, &xs, NULL, NULL, &xm, NULL, NULL);
	checkPetscError(ierr);
	// Get the size of the total grid
	ierr = DMDAGetInfo(da, PETSC_IGNORE, &Mx, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE);
	checkPetscError(ierr);

	// Setup step size variable
	double hx = 8.0 / (PetscReal) (Mx - 1);

	if (procId == 0) {
		// Initialize the HDF5 file
		xolotlCore::HDF5Utils::initializeFile(timestep, networkSize);

		// Get the physical dimension of the grid
		int dimension = (Mx - 1) * hx;

		// Get the refinement of the grid
		PetscInt refinement = 0;
		ierr = DMDAGetRefinementFactor(da, &refinement, PETSC_IGNORE, PETSC_IGNORE);
		checkPetscError(ierr);

		// Get the current time step
		PetscReal currentTimeStep;
		ierr = TSGetTimeStep(ts, &currentTimeStep);

		// Save the header in the HDF5 file
		xolotlCore::HDF5Utils::fillHeader(dimension, refinement, time, currentTimeStep);

		// Save the network in the HDF5 file
		xolotlCore::HDF5Utils::fillNetwork(PetscSolver::getNetwork());

		// Loop on the grid
		for (int xi = xs; xi < xs + xm; xi++) {
			// Get the pointer to the beginning of the solution data for this grid point
			gridPointSolution = solutionArray + networkSize * xi;
			// Update the concentrations in the network to have physics results
			// (non negative)
			PetscSolver::getNetwork()->updateConcentrationsFromArray(
					gridPointSolution);
			// Get the concentrations from the network
			double concentrations[networkSize];
			double * concentration = &concentrations[0];
			PetscSolver::getNetwork()->fillConcentrationsArray(concentration);

			// Fill the concentrations dataset in the HDF5 file
			xolotlCore::HDF5Utils::fillConcentrations(concentration,
					(double) xi * hx);
		}

		// Loop on the other processes
		for (int i = 1; i < worldSize; i++) {
			// Get the size of the local grid of that process
			int localSize = 0;
			MPI_Recv(&localSize, 1, MPI_INT, i, 0, MPI_COMM_WORLD,
					MPI_STATUS_IGNORE);

			// Loop on their grid
			for (int k = 0; k < localSize; k++) {
				// Get the position
				double x = 0.0;
				MPI_Recv(&x, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
						MPI_STATUS_IGNORE);

				// Initialize the array that will receive the concentrations
				double concentrations[networkSize];
				// Loop on the network
				for (int j = 0; j < networkSize; j++) {
					// Get the concentration
					double conc = 0.0;
					MPI_Recv(&conc, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
							MPI_STATUS_IGNORE);

					// Give it to the array
					concentrations[j] = conc;
				}

				// Fill the concentrations dataset in the HDF5 file
				xolotlCore::HDF5Utils::fillConcentrations(concentrations, x);
			}
		}
		// Finalize the HDF5 file
		xolotlCore::HDF5Utils::finalizeFile();
	}

	else {
		// Send the value of the local grid size to the master process
		MPI_Send(&xm, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);

		// Loop on the grid
		for (int xi = xs; xi < xs + xm; xi++) {
			// Get the pointer to the beginning of the solution data for this grid point
			gridPointSolution = solutionArray + networkSize * xi;
			// Update the concentrations in the network to have physics results
			// (non negative)
			PetscSolver::getNetwork()->updateConcentrationsFromArray(
					gridPointSolution);
			// Get the concentrations from the network
			double concentrations[networkSize];
			double * concentration = &concentrations[0];
			PetscSolver::getNetwork()->fillConcentrationsArray(concentration);

			// Send the value of the local position to the master process
			double x = xi * hx;
			MPI_Send(&x, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);

			// Loop on the network
			for (int j = 0; j < networkSize; j++) {
				// Send the value of the concentration to the master process
				MPI_Send(&concentration[j], 1, MPI_DOUBLE, 0, 0,
						MPI_COMM_WORLD);
			}
		}
	}

	PetscFunctionReturn(0);
}

/**
 * This is a monitoring method that will compute the total helium fluence
 */
static PetscErrorCode heliumRetention(TS ts, PetscInt timestep, PetscReal time,
		Vec solution, void *ictx) {
	// Network size
	const int size = PetscSolver::getNetwork()->size();
	PetscErrorCode ierr;
	PetscInt xs, xm, Mx;

	PetscFunctionBeginUser;

	// Variable to represent the real, or current, time
	PetscReal realTime;
	ierr = TSGetTime(ts, &realTime);

	// Get the process ID
	int procId;
	MPI_Comm_rank(MPI_COMM_WORLD, &procId);

	// Get the flux handler that will be used to compute fluxes.
	auto fluxHandler = PetscSolver::getFluxHandler();

	// Get the da from ts
	DM da;
	ierr = TSGetDM(ts, &da);
	checkPetscError(ierr);

	// Get the corners of the grid
	ierr = DMDAGetCorners(da, &xs, NULL, NULL, &xm, NULL, NULL);
	checkPetscError(ierr);
	// Get the size of the total grid
	ierr = DMDAGetInfo(da, PETSC_IGNORE, &Mx, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE);
	checkPetscError(ierr);

	// Setup step size variable
	double hx = 8.0 / (PetscReal) (Mx - 1);

	// Get the helium cluster
	auto heCluster = std::dynamic_pointer_cast < PSICluster
			> (PetscSolver::getNetwork()->get("He", 1));

	// Exit if there is no helium cluster in the network
	if (!heCluster)
		PetscFunctionReturn(0);

	// Get the composition of the cluster
	auto thisComp = heCluster->getComposition();
	std::vector<int> compVec = { thisComp["He"], thisComp["V"], thisComp["I"] };

	// Define the reactant ID
	int reactantIndex = heCluster->getId() - 1;

	// Loop on the grid
	for (int xi = xs; xi < xs + xm; xi++) {
		// Actual position in nm
		double x = xi * hx;

		// Vector representing the position at which the flux will be calculated
		// Currently we are only in 1D
		std::vector<double> gridPosition = { 0, x, 0 };

		// Calculate the incident flux
		auto incidentFlux = fluxHandler->getIncidentFlux(compVec, gridPosition,
				realTime);

		// And add it to the fluence
		heliumFluence += 10000.0 * incidentFlux * time;
	}

	PetscFunctionReturn(0);
}

/**
 * This is a monitoring method that will save 1D plots of one concentration
 */
static PetscErrorCode monitorScatter(TS ts, PetscInt timestep, PetscReal time,
		Vec solution, void *ictx) {
	// Network size
	const int networkSize = PetscSolver::getNetwork()->size();
	PetscErrorCode ierr;
	PetscReal *solutionArray, *gridPointSolution, x, hx;
	Vec localSolution;
	PetscInt xs, xm, Mx;
	int xi, i;

	PetscFunctionBeginUser;

	// Get the number of processes
	int worldSize;
	MPI_Comm_size(PETSC_COMM_WORLD, &worldSize);

	// Gets the process ID (important when it is running in parallel)
	int procId;
	MPI_Comm_rank(MPI_COMM_WORLD, &procId);

	// Get the da from ts
	DM da;
	ierr = TSGetDM(ts, &da);
	checkPetscError(ierr);

	// Get the local vector, which is capital when running in parallel,
	// and put it into solutionArray
	ierr = DMGetLocalVector(da, &localSolution);
	checkPetscError(ierr);
	ierr = DMGlobalToLocalBegin(da, solution, INSERT_VALUES, localSolution);
	checkPetscError(ierr);
	ierr = DMGlobalToLocalEnd(da, solution, INSERT_VALUES, localSolution);
	checkPetscError(ierr);
	ierr = DMDAVecGetArray(da, localSolution, &solutionArray);
	checkPetscError(ierr);

	// Get the corners of the grid
	ierr = DMDAGetCorners(da, &xs, NULL, NULL, &xm, NULL, NULL);
	checkPetscError(ierr);
	ierr = DMDAGetInfo(da, PETSC_IGNORE, &Mx, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE);
	checkPetscError(ierr);
	// Setup some step size variables
	hx = 8.0 / (PetscReal) (Mx - 1);

	// Choice of the cluster to be plotted
	int iCluster = 7;

	if (procId == 0) {
		// The array of cluster names
		std::vector<std::string> names(networkSize);

		// Fill the array of clusters name because the Id is not the same as
		// reactants->at(i)
		auto reactants = PetscSolver::getNetwork()->getAll();
		std::shared_ptr<PSICluster> cluster;

		// Loop on the reactants
		for (int i = 0; i < networkSize; i++) {

			// Get the cluster from the list, its id and composition
			cluster = std::dynamic_pointer_cast < PSICluster
					> (reactants->at(i));
			int id = cluster->getId() - 1;
			auto composition = cluster->getComposition();

			// Create the name
			std::stringstream name;
			name << (cluster->getName()).c_str() << "(" << composition["He"]
					<< "," << composition["V"] << "," << composition["I"]
					<< ") ";

			// Push the header entry on the array
			name >> names[id];
		}

		// Create a Point vector to store the data to give to the data provider
		// for the visualization
		auto myPoints = std::make_shared<std::vector<xolotlViz::Point> >();

		// Loop on the grid
		for (xi = xs; xi < xs + xm; xi++) {
			// Dump x
			x = xi * hx;
			// Get the pointer to the beginning of the solution data for this grid point
			gridPointSolution = solutionArray + networkSize * xi;
			// Update the concentrations in the network to have physics results
			// (non negative)
			PetscSolver::getNetwork()->updateConcentrationsFromArray(
					gridPointSolution);
			// Get the concentrations from the network
			double concentrations[networkSize];
			double * concentration = &concentrations[0];
			PetscSolver::getNetwork()->fillConcentrationsArray(concentration);

			// Create a Point with the concentration[iCluster] as the value
			// and add it to myPoints
			xolotlViz::Point aPoint;
			aPoint.value = concentration[iCluster];
			aPoint.t = time;
			aPoint.x = x;
			myPoints->push_back(aPoint);
		}

		// Loop on the other processes
		for (int i = 1; i < worldSize; i++) {
			// Get the size of the local grid of that process
			int localSize = 0;
			MPI_Recv(&localSize, 1, MPI_INT, i, 0, MPI_COMM_WORLD,
					MPI_STATUS_IGNORE);

			// Loop on their grid
			for (int k = 0; k < localSize; k++) {
				// Get the position
				MPI_Recv(&x, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
						MPI_STATUS_IGNORE);

				// and the concentration
				double conc = 0.0;
				MPI_Recv(&conc, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
						MPI_STATUS_IGNORE);

				// Create a Point with the concentration[iCluster] as the value
				// and add it to myPoints
				xolotlViz::Point aPoint;
				aPoint.value = conc;
				aPoint.t = time;
				aPoint.x = x;
				myPoints->push_back(aPoint);
			}
		}

		// Get the data provider and give it the points
		plot->getDataProvider()->setPoints(myPoints);

		// Change the title of the plot
		std::stringstream title;
		title << names[iCluster] << "_scatter_TS" << timestep << ".pnm";
		plot->plotLabelProvider->titleLabel = title.str();

		// Render
		plot->render();
	}

	else {
		// Send the value of the local grid size to the master process
		MPI_Send(&xm, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);

		// Loop on the grid
		for (xi = xs; xi < xs + xm; xi++) {
			// Dump x
			x = xi * hx;

			// Get the pointer to the beginning of the solution data for this grid point
			gridPointSolution = solutionArray + networkSize * xi;
			// Update the concentrations in the network to have physics results
			// (non negative)
			PetscSolver::getNetwork()->updateConcentrationsFromArray(
					gridPointSolution);
			// Get the concentrations from the network
			double concentrations[networkSize];
			double * concentration = &concentrations[0];
			PetscSolver::getNetwork()->fillConcentrationsArray(concentration);

			// Send the value of the local position to the master process
			MPI_Send(&x, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);

			// Send the value of the concentration to the master process
			MPI_Send(&concentration[iCluster], 1, MPI_DOUBLE, 0, 0,
					MPI_COMM_WORLD);
		}
	}

	PetscFunctionReturn(0);
}

/**
 * This is a monitoring method that will save 1D plots of many concentrations
 */
static PetscErrorCode monitorSeries(TS ts, PetscInt timestep, PetscReal time,
		Vec solution, void *ictx) {
	// Network size
	const int networkSize = PetscSolver::getNetwork()->size();
	PetscErrorCode ierr;
	PetscReal *solutionArray, *gridPointSolution, x, hx;
	Vec localSolution;
	PetscInt xs, xm, Mx;
	int xi, i;

	PetscFunctionBeginUser;

	// Get the number of processes
	int worldSize;
	MPI_Comm_size(PETSC_COMM_WORLD, &worldSize);

	// Gets the process ID (important when it is running in parallel)
	int procId;
	MPI_Comm_rank(MPI_COMM_WORLD, &procId);

	// Get the da from ts
	DM da;
	ierr = TSGetDM(ts, &da);
	checkPetscError(ierr);

	// Get the local vector, which is capital when running in parallel,
	// and put it into solutionArray
	ierr = DMGetLocalVector(da, &localSolution);
	checkPetscError(ierr);
	ierr = DMGlobalToLocalBegin(da, solution, INSERT_VALUES, localSolution);
	checkPetscError(ierr);
	ierr = DMGlobalToLocalEnd(da, solution, INSERT_VALUES, localSolution);
	checkPetscError(ierr);
	ierr = DMDAVecGetArray(da, localSolution, &solutionArray);
	checkPetscError(ierr);

	// Get the corners of the grid
	ierr = DMDAGetCorners(da, &xs, NULL, NULL, &xm, NULL, NULL);
	checkPetscError(ierr);
	ierr = DMDAGetInfo(da, PETSC_IGNORE, &Mx, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE);
	checkPetscError(ierr);
	// Setup some step size variables
	hx = 8.0 / (PetscReal) (Mx - 1);

	if (procId == 0) {

		// Create a Point vector to store the data to give to the data provider
		// for the visualization
		auto myPoints = std::make_shared<std::vector<xolotlViz::Point> >();
		auto myPointsBis = std::make_shared<std::vector<xolotlViz::Point> >();
		auto myPointsTer = std::make_shared<std::vector<xolotlViz::Point> >();
		auto myPointsQua = std::make_shared<std::vector<xolotlViz::Point> >();
		auto myPointsCin = std::make_shared<std::vector<xolotlViz::Point> >();

		// Loop on the grid
		for (xi = xs; xi < xs + xm; xi++) {
			// Dump x
			x = xi * hx;
			// Get the pointer to the beginning of the solution data for this grid point
			gridPointSolution = solutionArray + networkSize * xi;
			// Update the concentrations in the network to have physics results
			// (non negative)
			PetscSolver::getNetwork()->updateConcentrationsFromArray(
					gridPointSolution);
			// Get the concentrations from the network
			double concentrations[networkSize];
			double * concentration = &concentrations[0];
			PetscSolver::getNetwork()->fillConcentrationsArray(concentration);

			// Create a Point with the concentration[iCluster] as the value
			// and add it to myPoints
			xolotlViz::Point aPoint;
			aPoint.value = concentration[2]; // He
			aPoint.t = time;
			aPoint.x = x;
			myPoints->push_back(aPoint);
			aPoint.value = concentration[11]; // V
			myPointsBis->push_back(aPoint);
			aPoint.value = concentration[12]; // He1V1
			myPointsTer->push_back(aPoint);
			aPoint.value = concentration[13]; // He2V1
			myPointsQua->push_back(aPoint);
			aPoint.value = concentration[29]; // He1V2
			myPointsCin->push_back(aPoint);
		}

		// Loop on the other processes
		for (int i = 1; i < worldSize; i++) {
			// Get the size of the local grid of that process
			int localSize = 0;
			MPI_Recv(&localSize, 1, MPI_INT, i, 0, MPI_COMM_WORLD,
					MPI_STATUS_IGNORE);

			// Loop on their grid
			for (int k = 0; k < localSize; k++) {
				// Get the position
				MPI_Recv(&x, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
						MPI_STATUS_IGNORE);

				// and the concentrations
				double conc = 0.0;
				MPI_Recv(&conc, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
						MPI_STATUS_IGNORE);

				// Create a Point with the concentration[iCluster] as the value
				// and add it to myPoints
				xolotlViz::Point aPoint;
				aPoint.value = conc; // He
				aPoint.t = time;
				aPoint.x = x;
				myPoints->push_back(aPoint);

				MPI_Recv(&conc, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
						MPI_STATUS_IGNORE);

				aPoint.value = conc; // V
				myPointsBis->push_back(aPoint);

				MPI_Recv(&conc, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
						MPI_STATUS_IGNORE);

				aPoint.value = conc; // He1V1
				myPointsTer->push_back(aPoint);

				MPI_Recv(&conc, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
						MPI_STATUS_IGNORE);

				aPoint.value = conc; // He2V1
				myPointsQua->push_back(aPoint);

				MPI_Recv(&conc, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
						MPI_STATUS_IGNORE);

				aPoint.value = conc; // He1V2
				myPointsCin->push_back(aPoint);
			}
		}

		// Get the data provider and give it the points
		seriesPlot->getDataProvider(0)->setPoints(myPoints);
		seriesPlot->getDataProvider(1)->setPoints(myPointsBis);
		seriesPlot->getDataProvider(2)->setPoints(myPointsTer);
		seriesPlot->getDataProvider(3)->setPoints(myPointsQua);
		seriesPlot->getDataProvider(4)->setPoints(myPointsCin);

		// Change the title of the plot
		std::stringstream title;
		title << "log_series_TS" << timestep << ".pnm";
		seriesPlot->plotLabelProvider->titleLabel = title.str();

		// Render
		seriesPlot->render();
	}

	else {
		// Send the value of the local grid size to the master process
		MPI_Send(&xm, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);

		// Loop on the grid
		for (xi = xs; xi < xs + xm; xi++) {
			// Dump x
			x = xi * hx;

			// Get the pointer to the beginning of the solution data for this grid point
			gridPointSolution = solutionArray + networkSize * xi;
			// Update the concentrations in the network to have physics results
			// (non negative)
			PetscSolver::getNetwork()->updateConcentrationsFromArray(
					gridPointSolution);
			// Get the concentrations from the network
			double concentrations[networkSize];
			double * concentration = &concentrations[0];
			PetscSolver::getNetwork()->fillConcentrationsArray(concentration);

			// Send the value of the local position to the master process
			MPI_Send(&x, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);

			// Send the value of the concentrations to the master process
			MPI_Send(&concentration[2], 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
			MPI_Send(&concentration[11], 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
			MPI_Send(&concentration[12], 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
			MPI_Send(&concentration[13], 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
			MPI_Send(&concentration[29], 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
		}
	}

	PetscFunctionReturn(0);
}

/**
 * This is a monitoring method that will save 2D plots of one concentration
 */
static PetscErrorCode monitorSurface(TS ts, PetscInt timestep, PetscReal time,
		Vec solution, void *ictx) {
	// Network size
	const int networkSize = PetscSolver::getNetwork()->size();
	PetscErrorCode ierr;
	PetscReal *solutionArray, *gridPointSolution, x, hx;
	Vec localSolution;
	PetscInt xs, xm, Mx;
	int xi, i;

	PetscFunctionBeginUser;

	// Get the number of processes
	int worldSize;
	MPI_Comm_size(PETSC_COMM_WORLD, &worldSize);

	// Gets the process ID
	int procId;
	MPI_Comm_rank(MPI_COMM_WORLD, &procId);

	// Get the da from ts
	DM da;
	ierr = TSGetDM(ts, &da);
	checkPetscError(ierr);

	// Get the local vector, which is capital when running in parallel,
	// and put it into solutionArray
	ierr = DMGetLocalVector(da, &localSolution);
	checkPetscError(ierr);
	ierr = DMGlobalToLocalBegin(da, solution, INSERT_VALUES, localSolution);
	checkPetscError(ierr);
	ierr = DMGlobalToLocalEnd(da, solution, INSERT_VALUES, localSolution);
	checkPetscError(ierr);
	ierr = DMDAVecGetArray(da, localSolution, &solutionArray);
	checkPetscError(ierr);

	// Get the corners of the grid
	ierr = DMDAGetCorners(da, &xs, NULL, NULL, &xm, NULL, NULL);
	checkPetscError(ierr);
	ierr = DMDAGetInfo(da, PETSC_IGNORE, &Mx, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE, PETSC_IGNORE,
	PETSC_IGNORE);
	checkPetscError(ierr);
	// Setup some step size variables
	hx = 8.0 / (PetscReal) (Mx - 1);

	// Choice of the cluster to be plotted
	int iCluster = 2;

	// Create a Point vector to store the data to give to the data provider
	// for the visualization
	auto myPoints = std::make_shared<std::vector<xolotlViz::Point> >();

	// The array of cluster names
	std::vector<std::string> names(networkSize);

	// Loop on Y
	for (int i = 0; i < (int) Mx; i++) {

		if (procId == 0) {

			// Fill the array of clusters name because the Id is not the same as
			// reactants->at(i)
			auto reactants = PetscSolver::getNetwork()->getAll();
			std::shared_ptr<PSICluster> cluster;

			// Loop on the reactants
			for (int i = 0; i < networkSize; i++) {

				// Get the cluster from the list, its id and composition
				cluster = std::dynamic_pointer_cast < PSICluster
						> (reactants->at(i));
				int id = cluster->getId() - 1;
				auto composition = cluster->getComposition();

				// Create the name
				std::stringstream name;
				name << (cluster->getName()).c_str() << "(" << composition["He"]
						<< "," << composition["V"] << "," << composition["I"]
						<< ") ";

				// Push the header entry on the array
				name >> names[id];
			}

			// Loop on X
			for (xi = xs; xi < xs + xm; xi++) {
				// Dump x
				x = xi * hx;
				// Get the pointer to the beginning of the solution data for this grid point
				gridPointSolution = solutionArray + networkSize * xi;
				// Update the concentrations in the network to have physics results
				// (non negative)
				PetscSolver::getNetwork()->updateConcentrationsFromArray(
						gridPointSolution);
				// Get the concentrations from the network
				double concentrations[networkSize];
				double * concentration = &concentrations[0];
				PetscSolver::getNetwork()->fillConcentrationsArray(
						concentration);

				// Create a Point with the concentration[iCluster] as the value
				// and add it to myPoints
				xolotlViz::Point aPoint;
				aPoint.value = concentration[2];
				aPoint.t = time;
				aPoint.x = x;
				aPoint.y = (double) i;
				myPoints->push_back(aPoint);
			}

			// Loop on the other processes
			for (int i = 1; i < worldSize; i++) {
				// Get the size of the local grid of that process
				int localSize = 0;
				MPI_Recv(&localSize, 1, MPI_INT, i, 0, MPI_COMM_WORLD,
						MPI_STATUS_IGNORE);
				// Loop on their grid X
				for (int l = 0; l < localSize; l++) {
					// Get the position
					MPI_Recv(&x, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
							MPI_STATUS_IGNORE);
					double y = 0.0;
					MPI_Recv(&y, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
							MPI_STATUS_IGNORE);

					// and the concentration
					double conc = 0.0;
					MPI_Recv(&conc, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
							MPI_STATUS_IGNORE);

					// Create a Point with the concentration[iCluster] as the value
					// and add it to myPoints
					xolotlViz::Point aPoint;
					aPoint.value = conc;
					aPoint.t = time;
					aPoint.x = x;
					aPoint.y = y;
					myPoints->push_back(aPoint);
				}
			}
		}

		else {
			// Send the value of the local grid size to the master process
			MPI_Send(&xm, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
			// Loop on X
			for (xi = xs; xi < xs + xm; xi++) {
				// Dump x
				x = xi * hx;
				// Get the pointer to the beginning of the solution data for this grid point
				gridPointSolution = solutionArray + networkSize * xi;
				// Update the concentrations in the network to have physics results
				// (non negative)
				PetscSolver::getNetwork()->updateConcentrationsFromArray(
						gridPointSolution);
				// Get the concentrations from the network
				double concentrations[networkSize];
				double * concentration = &concentrations[0];
				PetscSolver::getNetwork()->fillConcentrationsArray(
						concentration);

				// Send the value of the local position to the master process
				MPI_Send(&x, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
				double y = (double) i;
				MPI_Send(&y, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);

				// Send the value of the concentration to the master process
				MPI_Send(&concentration[iCluster], 1, MPI_DOUBLE, 0, 0,
						MPI_COMM_WORLD);
			}
		}
	}

	if (procId == 0) {
		// Get the data provider and give it the points
		surfacePlot->getDataProvider()->setPoints(myPoints);

		// Change the title of the plot
		std::stringstream title;
		title << names[iCluster] << "_surface_TS" << timestep << ".pnm";
		surfacePlot->plotLabelProvider->titleLabel = title.str();

		// Render
		surfacePlot->render();
	}

	PetscFunctionReturn(0);
}

/**
 * This is a monitoring method that will save 1D plots of one performance timer
 */
static PetscErrorCode monitorPerf(TS ts, PetscInt timestep, PetscReal time,
		Vec solution, void *ictx) {

	PetscFunctionBeginUser;

	// Get the number of processes
	int size;
	MPI_Comm_size(PETSC_COMM_WORLD, &size);

	// Print a warning if only one process
	if (size == 1) {
		std::cout << "You are trying to plot things that don't have any sense!! "
				<< "\nRemove -plot_perf or run in parallel." << std::endl;
		PetscFunctionReturn(0);
	}

	// Get the current process ID
	int procId;
	MPI_Comm_rank(PETSC_COMM_WORLD, &procId);

	// Get the solve timer
	auto solverTimer = xolotlPerf::getHandlerRegistry()->getTimer("solve");

	// Stop it to access its value
	solverTimer->stop();

	// Master process
	if (procId == 0) {

		// Create a Point vector to store the data to give to the data provider
		// for the visualization
		auto myPoints = std::make_shared<std::vector<xolotlViz::Point> >();

		// Give it the value for procId = 0
		xolotlViz::Point aPoint;
		aPoint.value = solverTimer->getValue();
		aPoint.t = time;
		aPoint.x = procId;
		myPoints->push_back(aPoint);

		// Loop on all the other processes
		for (int i = 1; i < size; i++) {
			double counter = 0.0;

			// Receive the value from the other processes
			MPI_Recv(&counter, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
					MPI_STATUS_IGNORE);

			// Give it the value for procId = i
			aPoint.value = counter;
			aPoint.t = time;
			aPoint.x = i;
			myPoints->push_back(aPoint);
		}

		// Get the data provider and give it the points
		perfPlot->getDataProvider()->setPoints(myPoints);

		// Change the title of the plot
		std::stringstream title;
		title << "timer_TS" << timestep << ".pnm";
		perfPlot->plotLabelProvider->titleLabel = title.str();

		// Render
		perfPlot->render();
	}

	else {
		double counter = solverTimer->getValue();

		// Send the value of the timer to the master process
		MPI_Send(&counter, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
	}

	// Restart the timer
	solverTimer->start();

	PetscFunctionReturn(0);
}

/**
 * This operation sets up a monitor that will call monitorSolve
 * @param ts The time stepper
 * @return A standard PETSc error code
 */
PetscErrorCode setupPetscMonitor(TS ts) {
	PetscErrorCode ierr;

	// Flags to launch the monitors or not
	PetscBool flag2DPlot, flag1DPlot, flagSeries, flagPerf, flagRetention,
			flagStatus;

	// Check the option -plot_perf
	ierr = PetscOptionsHasName(NULL, "-plot_perf", &flagPerf);
	checkPetscError(ierr);

	// Check the option -plot_series
	ierr = PetscOptionsHasName(NULL, "-plot_series", &flagSeries);
	checkPetscError(ierr);

	// Check the option -plot_1d
	ierr = PetscOptionsHasName(NULL, "-plot_1d", &flag1DPlot);
	checkPetscError(ierr);

	// Check the option -plot_2d
	ierr = PetscOptionsHasName(NULL, "-plot_2d", &flag2DPlot);
	checkPetscError(ierr);

	// Check the option -helium_retention
	ierr = PetscOptionsHasName(NULL, "-helium_retention", &flagRetention);
	checkPetscError(ierr);

	// Check the option -start_stop
	ierr = PetscOptionsHasName(NULL, "-start_stop", &flagStatus);
	checkPetscError(ierr);

	// Use this instead of trying every flag
	int bigFlag = flag1DPlot + flag2DPlot + flagSeries + flagPerf
			+ flagRetention + flagStatus;

	// Don't do anything if no option is set
	if (!bigFlag)
		PetscFunctionReturn(0);

	// Set the monitor to save 1D plot of one concentration
	if (flag1DPlot) {
		// Create a ScatterPlot
		plot = vizHandlerRegistry->getPlot("scatterPlot", xolotlViz::PlotType::SCATTER);

		// Create and set the label provider
		auto labelProvider = std::make_shared<xolotlViz::LabelProvider>("labelProvider");
		labelProvider->axis1Label = "x Position on the Grid";
		labelProvider->axis2Label = "Concentration";

		// Give it to the plot
		plot->setLabelProvider(labelProvider);

		// Create the data provider
		auto dataProvider = std::make_shared<xolotlViz::CvsXDataProvider>("dataProvider");

		// Give it to the plot
		plot->setDataProvider(dataProvider);

		// monitorSolve will be called at each timestep
		ierr = TSMonitorSet(ts, monitorScatter, NULL, NULL);
		checkPetscError(ierr);
	}

	// Set the monitor to save 1D plot of many concentrations
	if (flagSeries) {
		// Create a ScatterPlot
		seriesPlot = vizHandlerRegistry->getPlot("seriesPlot", xolotlViz::PlotType::SERIES);

		// set the log scale
		seriesPlot->setLogScale();

		// Create and set the label provider
		auto labelProvider = std::make_shared<xolotlViz::LabelProvider>("labelProvider");
		labelProvider->axis1Label = "x Position on the Grid";
		labelProvider->axis2Label = "Concentration";

		// Give it to the plot
		seriesPlot->setLabelProvider(labelProvider);

		// Create the data provider
		auto dataProvider = std::make_shared<xolotlViz::CvsXDataProvider>("dataProvider");
		auto dataProviderBis = std::make_shared<xolotlViz::CvsXDataProvider>("dataProviderBis");
		auto dataProviderTer = std::make_shared<xolotlViz::CvsXDataProvider>("dataProviderTer");
		auto dataProviderQua = std::make_shared<xolotlViz::CvsXDataProvider>("dataProviderQua");
		auto dataProviderCin = std::make_shared<xolotlViz::CvsXDataProvider>("dataProviderCin");

		// Give it to the plot
		seriesPlot->addDataProvider(dataProvider);
		seriesPlot->addDataProvider(dataProviderBis);
		seriesPlot->addDataProvider(dataProviderTer);
		seriesPlot->addDataProvider(dataProviderQua);
		seriesPlot->addDataProvider(dataProviderCin);

		// monitorSolve will be called at each timestep
		ierr = TSMonitorSet(ts, monitorSeries, NULL, NULL);
		checkPetscError(ierr);
	}

	// Set the monitor to save surface plots of one concentration
	if (flag2DPlot) {
		// Create a SurfacePlot
		surfacePlot = vizHandlerRegistry->getPlot("surfacePlot", xolotlViz::PlotType::SURFACE);

		// Create and set the label provider
		auto labelProvider = std::make_shared<xolotlViz::LabelProvider>("labelProvider");
		labelProvider->axis1Label = "x Position on the Grid";
		labelProvider->axis2Label = "y Position on the Grid";
		labelProvider->axis3Label = "Concentration";

		// Give it to the plot
		surfacePlot->setLabelProvider(labelProvider);

		// Create the data provider
		auto dataProvider = std::make_shared<xolotlViz::CvsXYDataProvider>("dataProvider");

		// Give it to the plot
		surfacePlot->setDataProvider(dataProvider);

		// monitorSeries will be called at each timestep
		ierr = TSMonitorSet(ts, monitorSurface, NULL, NULL);
		checkPetscError(ierr);
	}

	// Set the monitor to save performance plots (has to be in parallel)
	if (flagPerf) {
		// Create a ScatterPlot
		perfPlot = vizHandlerRegistry->getPlot("perfPlot", xolotlViz::PlotType::SCATTER);

		// Create and set the label provider
		auto labelProvider = std::make_shared<xolotlViz::LabelProvider>("labelProvider");
		labelProvider->axis1Label = "Process ID";
		labelProvider->axis2Label = "Solver Time";

		// Give it to the plot
		perfPlot->setLabelProvider(labelProvider);

		// Create the data provider
		auto dataProvider = std::make_shared<xolotlViz::CvsXDataProvider>("dataProvider");

		// Give it to the plot
		perfPlot->setDataProvider(dataProvider);

		// monitorPerf will be called at each timestep
		ierr = TSMonitorSet(ts, monitorPerf, NULL, NULL);
		checkPetscError(ierr);

	}

	// Set the monitor to compute the helium fluence for the retention calculation
	if (flagRetention) {
		// heliumRetention will be called at each timestep
		ierr = TSMonitorSet(ts, heliumRetention, NULL, NULL);
		checkPetscError(ierr);
	}

	// Set the monitor to save the status of the simulation in hdf5 file
	if (flagStatus) {
		// startStop will be called at each timestep
		ierr = TSMonitorSet(ts, startStop, NULL, NULL);
		checkPetscError(ierr);
	}

	PetscFunctionReturn(0);
}

/**
 * This operation computes and prints the helium retention.
 * @param ts The time stepper.
 * @param C The vector of solution.
 */
void computeRetention(TS ts, Vec C) {
	PetscErrorCode ierr;
	// Network size
	const int size = PetscSolver::getNetwork()->size();

	// Get the helium cluster
	auto heCluster = std::dynamic_pointer_cast < PSICluster
			> (PetscSolver::getNetwork()->get("He", 1));

	if (!heCluster) {
		throw std::string(
				"PetscSolver Exception: Cannot compute the retention because there is no helium1 in the network.");
		return;
	}

	// Keep the ID of the helium
	int reactantIndex = heCluster->getId() - 1;

	// Get the da from ts
	DM da;
	ierr = TSGetDM(ts, &da);
	checkPetscError(ierr);

	// Get the array of concentration
	PetscReal *solutionArray;
	ierr = DMDAVecGetArray(da, C, &solutionArray);
	checkPetscError(ierr);

	//Get local grid boundaries
	PetscInt xs, xm;
	ierr = DMDAGetCorners(da, &xs, NULL, NULL, &xm, NULL, NULL);
	checkPetscError(ierr);

	// Store the concentration over the grid
	double heConcentration = 0;

	// Loop on the grid
	for (int xi = xs; xi < xs + xm; xi++) {
		// Get the pointer to the beginning of the solution data for this grid point
		PetscReal *gridPointSolution;
		gridPointSolution = solutionArray + size * xi;

		// Update the concentrations in the network to have physics results
		// (non negative)
		PetscSolver::getNetwork()->updateConcentrationsFromArray(
				gridPointSolution);

		// Get the concentrations from the network
		double concentrations[size];
		double * concentration = &concentrations[0];
		PetscSolver::getNetwork()->fillConcentrationsArray(concentration);

		// Add the current concentration
		heConcentration += concentration[reactantIndex];
	}

	// Get the number of processes
	int worldSize;
	MPI_Comm_size(PETSC_COMM_WORLD, &worldSize);
	// Get the current process ID
	int procId;
	MPI_Comm_rank(MPI_COMM_WORLD, &procId);

	// Master process
	if (procId == 0) {
		// Loop on all the other processes
		for (int i = 1; i < worldSize; i++) {
			double otherConcentration = 0.0;
			double otherFluence = 0.0;

			// Receive the value from the other processes
			MPI_Recv(&otherConcentration, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
					MPI_STATUS_IGNORE);
			MPI_Recv(&otherFluence, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD,
					MPI_STATUS_IGNORE);

			// Add them to the master one
			heConcentration += otherConcentration;
			heliumFluence += otherFluence;
		}

		// Print the result
		std::cout << "Helium retention = "
				<< 100.0 * heConcentration / heliumFluence << " %" << std::endl;
	}

	else {
		// Send the value of the timer to the master process
		MPI_Send(&heConcentration, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
		MPI_Send(&heliumFluence, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
	}
	return;
}

}

/* end namespace xolotlSolver */
