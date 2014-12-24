// Includes
#include <PetscSolver.h>
#include <HDF5NetworkLoader.h>
#include <HDF5Utils.h>

using namespace xolotlCore;

/*
 C_t =  -D*C_xx + A*C_x + F(C) + R(C) + D(C) from Brian Wirth's SciDAC project.

 D*C_xx  - diffusion of He and V and I
 A*C_x   - advection of He
 F(C)    - forcing function; He being created.
 R(C)    - reaction terms   (clusters combining)
 D(C)    - dissociation terms (cluster breaking up)

 Sample Options:
 -da_grid_x <nx>						 -- number of grid points in the x direction
 -ts_max_steps <maxsteps>                -- maximum number of time-steps to take
 -ts_final_time <time>                   -- maximum time to compute to
 -ts_dt <size>							 -- initial size of the time step

 */

namespace xolotlSolver {

//Timer for RHSFunction()
std::shared_ptr<xolotlPerf::ITimer> RHSFunctionTimer;

////Timer for RHSJacobian()
std::shared_ptr<xolotlPerf::ITimer> RHSJacobianTimer;

//! Help message
static char help[] =
		"Solves C_t =  -D*C_xx + A*C_x + F(C) + R(C) + D(C) from Brian Wirth's SciDAC project.\n";

// ----- GLOBAL VARIABLES ----- //

extern PetscErrorCode RHSFunction(TS, PetscReal, Vec, Vec, void*);
extern PetscErrorCode RHSJacobian(TS, PetscReal, Vec, Mat, Mat);
extern PetscErrorCode setupPetscMonitor(TS);

/**
 * A boolean that is true if the temperature has changed.
 */
static bool temperatureChanged = false;

/* ----- Error Handling Code ----- */

/**
 * This operation checks a Petsc error code and converts it to a bool.
 * @param errorCode The Petsc error code.
 * @return True if everything is OK, false otherwise.
 */
static inline bool checkPetscError(PetscErrorCode errorCode) {
	CHKERRQ(errorCode);
}

/**
 * This operation "returns" in a way that Petsc expects.
 * @return The return code from Petsc.
 */
static inline int petscReturn() {
	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "setupInitialConditions"
PetscErrorCode PetscSolver::setupInitialConditions(DM da, Vec C) {

	// Local Declarations
	PetscErrorCode ierr;
	PetscInt i, nI, nHe, nV, cnt = 0;
	char string[16];
	auto allReactants = network->getAll();
	int dof = allReactants->size();
	std::map<std::string, int> composition;

	/* Name each of the concentrations */
	for (i = 0; i < dof; i++) {
		composition = allReactants->at(i)->getComposition();
		nHe = composition["He"];
		nV = composition["V"];
		nI = composition["I"];
		ierr = PetscSNPrintf(string, 16, "He-%d,V-%d,I-%d", nHe, nV, nI);
		checkPetscError(ierr);
		ierr = DMDASetFieldName(da, cnt++, string);
		checkPetscError(ierr);
	}

	// Initialize the concentrations in the solution vector
	auto solverHandler = PetscSolver::getSolverHandler();
	solverHandler->initializeConcentration(da, C);

	PetscFunctionReturn(0);
}

/* ------------------------------------------------------------------- */

#undef __FUNCT__
#define __FUNCT__ "RHSFunction"
/*
 RHSFunction - Evaluates the right-hand-side of the nonlinear function defining the ODE

 Input Parameters:
 .  ts - the TS context
 .  ftime - the physical time at which the function is evaluated
 .  C - input vector
 .  ptr - optional user-defined context

 Output Parameter:
 .  F - function values
 */
/* ------------------------------------------------------------------- */
PetscErrorCode RHSFunction(TS ts, PetscReal ftime, Vec C, Vec F, void *ptr) {
	// Start the RHSFunction Timer
	RHSFunctionTimer->start();

	PetscErrorCode ierr;

	// Get the local data vector from petsc
	DM da;
	ierr = TSGetDM(ts, &da);
	checkPetscError(ierr);
	Vec localC;
	ierr = DMGetLocalVector(da, &localC);
	checkPetscError(ierr);

	// Scatter ghost points to local vector, using the 2-step process
	// DMGlobalToLocalBegin(),DMGlobalToLocalEnd().
	// By placing code between these two statements, computations can be
	// done while messages are in transition.
	ierr = DMGlobalToLocalBegin(da, C, INSERT_VALUES, localC);
	checkPetscError(ierr);
	ierr = DMGlobalToLocalEnd(da, C, INSERT_VALUES, localC);
	checkPetscError(ierr);

	// Set the initial values of F
	ierr = VecSet(F, 0.0);
	checkPetscError(ierr);

	// Compute the new concentrations
	auto solverHandler = PetscSolver::getSolverHandler();
	solverHandler->updateConcentration(ts, localC, F, ftime, temperatureChanged);

	// Stop the RHSFunction Timer
	RHSFunctionTimer->stop();

	PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "RHSJacobian"
/*
 Compute the Jacobian entries based on IFunction() and insert them into the matrix
 */
PetscErrorCode RHSJacobian(TS ts, PetscReal ftime, Vec C, Mat A, Mat J,
		void *ptr) {
	// Start the RHSJacobian timer
	RHSJacobianTimer->start();

	PetscErrorCode ierr;

	// Get the matrix from PETSc
	PetscFunctionBeginUser;
	ierr = MatZeroEntries(J);
	checkPetscError(ierr);
	DM da;
	ierr = TSGetDM(ts, &da);
	checkPetscError(ierr);
	Vec localC;
	ierr = DMGetLocalVector(da, &localC);
	checkPetscError(ierr);

	// Get the complete data array
	ierr = DMGlobalToLocalBegin(da, C, INSERT_VALUES, localC);
	checkPetscError(ierr);
	ierr = DMGlobalToLocalEnd(da, C, INSERT_VALUES, localC);
	checkPetscError(ierr);

	// Get the solver handler
	auto solverHandler = PetscSolver::getSolverHandler();

	// Only compute the off-diagonal part of the Jacobian if the temperature has changed
	if (temperatureChanged) {

		// Compute the off-diagonal part of the Jacobian
		solverHandler->computeOffDiagonalJacobian(ts, localC, J);

		ierr = MatAssemblyBegin(J, MAT_FINAL_ASSEMBLY);
		checkPetscError(ierr);
		ierr = MatAssemblyEnd(J, MAT_FINAL_ASSEMBLY);
		checkPetscError(ierr);
//		ierr = MatSetOption(J, MAT_NEW_NONZERO_LOCATIONS, PETSC_FALSE);
//		checkPetscError(ierr);
		ierr = MatStoreValues(J);
		checkPetscError(ierr);
//		MatSetFromOptions(J);
		temperatureChanged = false;
		// Debug line for viewing the matrix
		//MatView(J, PETSC_VIEWER_STDOUT_WORLD);
	} else {
		ierr = MatRetrieveValues(J);
		checkPetscError(ierr);
	}

	/* ----- Compute the partial derivatives for the reaction term ----- */
	solverHandler->computeDiagonalJacobian(ts, localC, J);

	ierr = MatAssemblyBegin(J, MAT_FINAL_ASSEMBLY);
	checkPetscError(ierr);
	ierr = MatAssemblyEnd(J, MAT_FINAL_ASSEMBLY);
	checkPetscError(ierr);

	if (A != J) {
		ierr = MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY);
		checkPetscError(ierr);
		ierr = MatAssemblyEnd(A, MAT_FINAL_ASSEMBLY);
		checkPetscError(ierr);
	}

	// Stop the RHSJacobian timer
	RHSJacobianTimer->stop();

	PetscFunctionReturn(0);
}

PetscSolver::PetscSolver(std::shared_ptr<xolotlPerf::IHandlerRegistry> registry) :
	Solver(registry) {
	RHSFunctionTimer = handlerRegistry->getTimer("RHSFunctionTimer");
	RHSJacobianTimer = handlerRegistry->getTimer("RHSJacobianTimer");
}

PetscSolver::~PetscSolver() {
}

void PetscSolver::setOptions(std::map<std::string, std::string> options) {
}

void PetscSolver::setupMesh() {
}

void PetscSolver::initialize(std::shared_ptr<ISolverHandler> solverHandler) {
	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 Initialize program
	 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	PetscInitialize(&numCLIArgs, &CLIArgs, (char*) 0, help);

	// Set the solver handler
	Solver::solverHandler = (ISolverHandler *) solverHandler.get();

	return;
}

void PetscSolver::solve() {
	PetscErrorCode ierr;

	// Check the network before getting busy.
	if (!network) {
		throw std::string("PetscSolver Exception: Network not set!");
	}

	// Create the solver context
	DM da;
	Solver::solverHandler->createSolverContext(da);

	/*  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 Extract global vector from DMDA to hold solution
	 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	Vec C;
	ierr = DMCreateGlobalVector(da, &C);
	checkPetscError(ierr);

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 Create timestepping solver context
	 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	TS ts;
	ierr = TSCreate(PETSC_COMM_WORLD, &ts);
	checkPetscError(ierr);
	ierr = TSSetType(ts, TSARKIMEX);
	checkPetscError(ierr);
	ierr = TSARKIMEXSetFullyImplicit(ts, PETSC_TRUE);
	checkPetscError(ierr);
	ierr = TSSetDM(ts, da);
	checkPetscError(ierr);
	ierr = TSSetProblemType(ts, TS_NONLINEAR);
	checkPetscError(ierr);
	ierr = TSSetRHSFunction(ts, NULL, RHSFunction, NULL);
	checkPetscError(ierr);
	ierr = TSSetRHSJacobian(ts, NULL, NULL, RHSJacobian, NULL);
	checkPetscError(ierr);
	ierr = TSSetSolution(ts, C);
	checkPetscError(ierr);

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 Set solver options
	 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	// Get the name of the HDF5 file to read the concentrations from
	auto HDF5Loader = (HDF5NetworkLoader *) networkLoader;
	auto fileName = HDF5Loader->getFilename();

	// Get starting conditions from HDF5 file
	int gridLength = 0;
	double time = 0.0, deltaTime = 1.0e-12;
	int tempTimeStep = -2;
	HDF5Utils::readHeader(fileName, gridLength);

	// Read the times if the information is in the HDF5 file
	if (HDF5Utils::hasConcentrationGroup(fileName, tempTimeStep)) {
		HDF5Utils::readTimes(fileName, tempTimeStep, time, deltaTime);
	}

	ierr = TSSetInitialTimeStep(ts, time, deltaTime);
	checkPetscError(ierr);
	ierr = TSSetFromOptions(ts);
	checkPetscError(ierr);

	ierr = setupPetscMonitor(ts);
	checkPetscError(ierr);

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 Set initial conditions
	 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	ierr = setupInitialConditions(da, C);
	checkPetscError(ierr);

	// Set the output precision for std::out
	std::cout.precision(16);

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 Solve the ODE system
	 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	if (ts != NULL && C != NULL) {
		ierr = TSSolve(ts, C);
		checkPetscError(ierr);
	} else {
		throw std::string(
				"PetscSolver Exception: Unable to solve! Data not configured properly.");
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 Free work space.
	 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	ierr = VecDestroy(&C);
	checkPetscError(ierr);
	ierr = TSDestroy(&ts);
	checkPetscError(ierr);
	ierr = DMDestroy(&da);
	checkPetscError(ierr);

	return;
}

void PetscSolver::finalize() {
	PetscErrorCode ierr;

	ierr = PetscFinalize();
	checkPetscError(ierr);
	if (petscReturn() != 0) {
		throw std::string("PetscSolver Exception: Unable to finalize solve!");
	}
}

} /* end namespace xolotlSolver */
