// Includes
#include <PetscSolver1DHandler.h>
#include <HDF5Utils.h>
#include <MathUtils.h>
#include <Constants.h>

namespace xolotlSolver 
{

//--------------------------------------------------------------------------------
 void 
 PetscSolver1DHandler::createSolverContext( DM& da ) 
 {
  PetscErrorCode ierr;

// Initialize the all reactants pointer
  allReactants = network->getAll();

// Set the last temperature to 0
  lastTemperature = 0.0;

// Reinitialize the connectivities in the network after updating the temperature
// Get the temperature from the temperature handler
  auto temperature = temperatureHandler->getTemperature( { 0.0, 0.0, 0.0 }, 0.0 );

// Set the temperature to compute all the rate constants
  if (!xolotlCore::equal(temperature, lastTemperature)) 
  {
// Update the temperature for all of the clusters
   int networkSize = network->size();
   for (int i = 0; i < networkSize; i++) 
   {
// This part will set the temperature in each reactant
// and recompute the diffusion coefficient
    allReactants->at(i)->setTemperature(temperature);
   }
   for (int i = 0; i < networkSize; i++) 
   {
// Now that the diffusion coefficients of all the reactants
// are updated, the reaction and dissociation rates can be
// recomputed
    allReactants->at(i)->computeRateConstants();
   }
   lastTemperature = temperature;
  }

// Recompute Ids and network size and redefine the connectivities
  network->reinitializeConnectivities();

// Degrees of freedom is the total number of clusters in the network
  const int dof = network->getDOF();

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 Create distributed array (DMDA) to manage parallel grid and vectors
 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

// Get starting conditions from HDF5 file
  int    nx = 0,   ny = 0,   nz = 0;
  double hx = 0.0, hy = 0.0, hz = 0.0;
  xolotlCore::HDF5Utils::readHeader(networkName, nx, hx, ny, hy, nz, hz);

  ierr = DMDACreate1d( PETSC_COMM_WORLD, DM_BOUNDARY_GHOSTED, nx, dof, 1,
                       NULL, &da );
  checkPetscError(ierr, "PetscSolver1DHandler::createSolverContext: DMDACreate1d failed.");
  ierr = DMSetFromOptions(da);
  checkPetscError(ierr, "PetscSolver1DHandler::createSolverContext: DMSetFromOptions failed.");
  ierr = DMSetUp(da);
  checkPetscError(ierr, "PetscSolver1DHandler::createSolverContext: DMSetUp failed.");

// Set the position of the surface
  surfacePosition = 0;
  if (movingSurface) surfacePosition = (int) (nx * portion / 100.0);

// Generate the grid in the x direction
  generateGrid(nx, hx, surfacePosition);

// Now that the grid was generated, we can update the surface position
// if we are using a restart file
  int tempTimeStep = -2;
  bool hasConcentrations = xolotlCore::HDF5Utils::hasConcentrationGroup( networkName, tempTimeStep);
  if (hasConcentrations) 
  {
   surfacePosition = xolotlCore::HDF5Utils::readSurface1D(networkName, tempTimeStep);
  }

// Initialize the surface of the first advection handler corresponding to the
// advection toward the surface (or a dummy one if it is deactivated)
  advectionHandlers[0]->setLocation( grid[surfacePosition] );

//	for (int i = 0; i < grid.size(); i++) {
//		std::cout << grid[i] - grid[surfacePosition] << " ";
//	}
//	std::cout << std::endl;

// Set the size of the partial derivatives vectors
  clusterPartials.resize(dof, 0.0);
  reactingPartialsForCluster.resize(dof, 0.0);

/*  The only spatial coupling in the Jacobian is due to diffusion.
 *  The ofill (thought of as a dof by dof 2d (row-oriented) array represents
 *  the nonzero coupling between degrees of freedom at one point with degrees
 *  of freedom on the adjacent point to the left or right. A 1 at i,j in the
 *  ofill array indicates that the degree of freedom i at a point is coupled
 *  to degree of freedom j at the adjacent point.
 *  In this case ofill has only a few diagonal entries since the only spatial
 *  coupling is regular diffusion.
 */
  PetscInt* ofill;
  PetscInt* dfill;
  ierr = PetscMalloc(dof * dof * sizeof(PetscInt), &ofill);
  checkPetscError(ierr, "PetscSolver1DHandler::createSolverContext: PetscMalloc (ofill) failed.");
  ierr = PetscMalloc(dof * dof * sizeof(PetscInt), &dfill);
  checkPetscError(ierr, "PetscSolver1DHandler::createSolverContext: PetscMalloc (dfill) failed.");
  ierr = PetscMemzero(ofill, dof * dof * sizeof(PetscInt));
  checkPetscError(ierr, "PetscSolver1DHandler::createSolverContext: PetscMemzero (ofill) failed.");
  ierr = PetscMemzero(dfill, dof * dof * sizeof(PetscInt));
  checkPetscError(ierr, "PetscSolver1DHandler::createSolverContext: PetscMemzero (dfill) failed.");

// Fill ofill, the matrix of "off-diagonal" elements that represents diffusion
  diffusionHandler->initializeOFill(network, ofill);
// Loop on the advection handlers to account the other "off-diagonal" elements
  for (int i = 0; i < advectionHandlers.size(); i++) 
  {
   advectionHandlers[i]->initialize(network, ofill);
  }

// Initialize the modified trap-mutation handler here
// because it adds connectivity
  mutationHandler->initialize( network, grid );
  mutationHandler->initializeIndex1D( surfacePosition, network, advectionHandlers, grid );

// Get the diagonal fill
  network->getDiagonalFill(dfill);

// Load up the block fills
  ierr = DMDASetBlockFills(da, dfill, ofill);
  checkPetscError(ierr, "PetscSolver1DHandler::createSolverContext: DMDASetBlockFills failed.");

// Free the temporary fill arrays
  ierr = PetscFree(ofill);
  checkPetscError(ierr, "PetscSolver1DHandler::createSolverContext: PetscFree (ofill) failed.");
  ierr = PetscFree(dfill);
  checkPetscError(ierr, "PetscSolver1DHandler::createSolverContext: PetscFree (dfill) failed.");

  return;
 }

//--------------------------------------------------------------------------------
 void 
 PetscSolver1DHandler::initializeConcentration( DM& da, Vec& C ) 
 {
  PetscErrorCode ierr;

// Pointer for the concentration vector
  PetscScalar** concentrations = nullptr;
  ierr = DMDAVecGetArrayDOF(da, C, &concentrations);
  checkPetscError(ierr, "PetscSolver1DHandler::initializeConcentration: "
                        "DMDAVecGetArrayDOF failed.");

// Get the local boundaries
  PetscInt xs, xm;
  ierr = DMDAGetCorners(da, &xs, NULL, NULL, &xm, NULL, NULL);
  checkPetscError(ierr, "PetscSolver1DHandler::initializeConcentration: "
                        "DMDAGetCorners failed.");

// Get the last time step written in the HDF5 file
  int tempTimeStep = -2;
  bool hasConcentrations = xolotlCore::HDF5Utils::hasConcentrationGroup( networkName, 
                                                  tempTimeStep);

// Get the total size of the grid for the boundary conditions
  int xSize = grid.size();

// Initialize the flux handler
  fluxHandler->initializeFluxHandler(network, surfacePosition, grid);

// Initialize the grid for the diffusion
  diffusionHandler->initializeDiffusionGrid(advectionHandlers, grid);

// Initialize the grid for the advection
  advectionHandlers[0]->initializeAdvectionGrid(advectionHandlers, grid);

// Pointer for the concentration vector at a specific grid point
  PetscScalar *concOffset = nullptr;

// Degrees of freedom is the total number of clusters in the network
// + the super clusters
  const int dof = network->getDOF();

// Get the single vacancy ID
  auto singleVacancyCluster = network->get(xolotlCore::vType, 1);
  int vacancyIndex = -1;
  if (singleVacancyCluster)
  vacancyIndex = singleVacancyCluster->getId() - 1;

// Loop on all the grid points
  for (PetscInt i = xs; i < xs + xm; i++) 
  {
   concOffset = concentrations[i];

// Loop on all the clusters to initialize at 0.0
   for (int n = 0; n < dof; n++) 
   {
    concOffset[n] = 0.0;
   }

// Initialize the vacancy concentration
   if (i > surfacePosition && i < xSize - 1 && singleVacancyCluster) 
   {
    concOffset[vacancyIndex] = initialVConc;
   }
  }

// If the concentration must be set from the HDF5 file
  if (hasConcentrations) 
  {
// Loop on the full grid
   for (int i = 0; i < xSize; i++) 
   {
// Read the concentrations from the HDF5 file
    auto concVector = xolotlCore::HDF5Utils::readGridPoint(networkName, tempTimeStep, i);

// Change the concentration only if we are on the locally owned part of the grid
    if (i >= xs && i < xs + xm) 
    {
     concOffset = concentrations[i];
// Loop on the concVector size
     for (unsigned int l = 0; l < concVector.size(); l++) 
     {
      concOffset[(int) concVector.at(l).at(0)] = concVector.at(l).at(1);
     }
    }
   }
  }
/*
 Restore vectors
 */
  ierr = DMDAVecRestoreArrayDOF( da, C, &concentrations );
         checkPetscError(ierr, "PetscSolver1DHandler::initializeConcentration: "
                               "DMDAVecRestoreArrayDOF failed.");

  return;
 }

//--------------------------------------------------------------------------------
 void 
 PetscSolver1DHandler::updateConcentration( TS& ts, Vec& localC, Vec& F, 
                                            PetscReal ftime )
 {
  PetscErrorCode ierr;

// Get the local data vector from PETSc
  DM da;
  ierr = TSGetDM(ts, &da);
  checkPetscError(ierr, "PetscSolver1DHandler::updateConcentration: "
			"TSGetDM failed.");

// Get the total size of the grid for the boundary conditions
  int xSize = grid.size();

// Pointers to the PETSc arrays that start at the beginning (xs) of the
// local array!
  PetscScalar **concs = nullptr, **updatedConcs = nullptr;
// Get pointers to vector data
  ierr = DMDAVecGetArrayDOFRead(da, localC, &concs);
  checkPetscError(ierr, "PetscSolver1DHandler::updateConcentration: "
                        "DMDAVecGetArrayDOFRead (localC) failed.");
  ierr = DMDAVecGetArrayDOF(da, F, &updatedConcs);
  checkPetscError(ierr, "PetscSolver1DHandler::updateConcentration: "
                        "DMDAVecGetArrayDOF (F) failed.");

// Get local grid boundaries
  PetscInt xs, xm;
  ierr = DMDAGetCorners(da, &xs, NULL, NULL, &xm, NULL, NULL);
  checkPetscError(ierr, "PetscSolver1DHandler::updateConcentration: "
                        "DMDAGetCorners failed.");

// The following pointers are set to the first position in the conc or
// updatedConc arrays that correspond to the beginning of the data for the
// current grid point. They are accessed just like regular arrays.
  PetscScalar* concOffset = nullptr, *updatedConcOffset = nullptr;

// Degrees of freedom is the total number of clusters in the network
  const int dof = network->getDOF();

// Compute the total concentration of atoms contained in bubbles
  double atomConc = 0.0;

// Loop over grid points to get the atom concentration
// near the surface
  for (int xi = xs; xi < xs + xm; xi++) 
  {
// Boundary conditions
   if ( xi <= surfacePosition || xi == xSize - 1 ) continue;

// We are only interested in the helium near the surface
   if ( grid[xi] - grid[surfacePosition] > 2.0 ) continue;

// Get the concentrations at this grid point
   concOffset = concs[xi];
// Copy data into the PSIClusterReactionNetwork
   network->updateConcentrationsFromArray(concOffset);

// Sum the total atom concentration
   atomConc += network->getTotalTrappedAtomConcentration() * (grid[xi] - grid[xi - 1]);
  }

// Share the concentration with all the processes
  double totalAtomConc = 0.0;
  MPI_Allreduce(&atomConc, &totalAtomConc, 1, MPI_DOUBLE, MPI_SUM,
                MPI_COMM_WORLD);

// Set the disappearing rate in the modified TM handler
  mutationHandler->updateDisappearingRate(totalAtomConc);

// Declarations for variables used in the loop
  double **concVector = new double*[3];
  std::vector<double> gridPosition = { 0.0, 0.0, 0.0 };

// Loop over grid points computing ODE terms for each grid point
  for (PetscInt xi = xs; xi < xs + xm; xi++) 
  {
// Compute the old and new array offsets
   concOffset = concs[xi];
   updatedConcOffset = updatedConcs[xi];

// Boundary conditions
// Everything to the left of the surface is empty
   if (xi <= surfacePosition || xi == xSize - 1) 
   {
    for (int i = 0; i < dof; i++) 
    {
     updatedConcOffset[i] = 1.0 * concOffset[i];
    }

    continue;
   }

// Set the grid position
   gridPosition[0] = grid[xi];

// Fill the concVector with the pointer to the middle, left, and right grid points
   concVector[0] = concOffset; // middle
   concVector[1] = concs[xi - 1]; // left
   concVector[2] = concs[xi + 1]; // right

// Get the temperature from the temperature handler
   auto temperature = temperatureHandler->getTemperature(gridPosition, ftime);

// Update the network if the temperature changed
   if (!xolotlCore::equal(temperature, lastTemperature)) 
   {
    network->setTemperature(temperature);
// Update the modified trap-mutation rate
// that depends on the network reaction rates
    mutationHandler->updateTrapMutationRate(network);
    lastTemperature = temperature;
   }

// Copy data into the ReactionNetwork so that it can
// compute the fluxes properly. The network is only used to compute the
// fluxes and hold the state data from the last time step. I'm reusing
// it because it cuts down on memory significantly (about 400MB per
// grid point) at the expense of being a little tricky to comprehend.
   network->updateConcentrationsFromArray(concOffset);

// ----- Account for flux of incoming particles -----
   fluxHandler->computeIncidentFlux(ftime, updatedConcOffset, xi, surfacePosition);

// ---- Compute diffusion over the locally owned part of the grid -----
   diffusionHandler->computeDiffusion( network, concVector,
                                       updatedConcOffset, grid[xi] - grid[xi - 1],
                                       grid[xi + 1] - grid[xi], xi );

// ---- Compute advection over the locally owned part of the grid -----
   for (int i = 0; i < advectionHandlers.size(); i++) 
   {
    advectionHandlers[i]->computeAdvection(network, gridPosition,
                          concVector, updatedConcOffset, grid[xi] - grid[xi - 1],
                          grid[xi + 1] - grid[xi], xi);
   }

// ----- Compute the modified trap-mutation over the locally owned part of the grid -----
   mutationHandler->computeTrapMutation(network, concOffset, updatedConcOffset, xi);

// ----- Compute the reaction fluxes over the locally owned part of the grid -----
   network->computeAllFluxes(updatedConcOffset);
  }

/*
 Restore vectors
 */
  ierr = DMDAVecRestoreArrayDOFRead(da, localC, &concs);
  checkPetscError(ierr, "PetscSolver1DHandler::updateConcentration: "
                        "DMDAVecRestoreArrayDOFRead (localC) failed.");
  ierr = DMDAVecRestoreArrayDOF(da, F, &updatedConcs);
  checkPetscError(ierr, "PetscSolver1DHandler::updateConcentration: "
                        "DMDAVecRestoreArrayDOF (F) failed.");
  ierr = DMRestoreLocalVector(da, &localC);
  checkPetscError(ierr, "PetscSolver1DHandler::updateConcentration: "
                        "DMRestoreLocalVector failed.");

// Clear memory
  delete[] concVector;

  return;
 }

//--------------------------------------------------------------------------------
 void 
 PetscSolver1DHandler::computeOffDiagonalJacobian( TS& ts, Vec& localC, Mat& J, 
                                                   PetscReal ftime) 
 {
  PetscErrorCode ierr;

// Get the distributed array
  DM da;
  ierr = TSGetDM(ts, &da);
  checkPetscError(ierr, "PetscSolver1DHandler::computeOffDiagonalJacobian: "
                        "TSGetDM failed.");

// Get the total size of the grid for the boundary conditions
  int xSize = grid.size();

// Get local grid boundaries
  PetscInt xs, xm;
  ierr = DMDAGetCorners(da, &xs, NULL, NULL, &xm, NULL, NULL);
  checkPetscError(ierr, "PetscSolver1DHandler::computeOffDiagonalJacobian: "
                        "DMDAGetCorners failed.");

// Get the total number of diffusing clusters
  const int nDiff = diffusionHandler->getNumberOfDiffusing();

// Get the total number of advecting clusters
  int nAdvec = 0;
  for (int l = 0; l < advectionHandlers.size(); l++) 
  {
   int n = advectionHandlers[l]->getNumberOfAdvecting();
   if (n > nAdvec) nAdvec = n;
  }

// Arguments for MatSetValuesStencil called below
  MatStencil row, cols[3];
  PetscScalar diffVals[3 * nDiff];
  PetscInt diffIndices[nDiff];
  PetscScalar advecVals[2 * nAdvec];
  PetscInt advecIndices[nAdvec];
  std::vector<double> gridPosition = { 0.0, 0.0, 0.0 };

/*
 Loop over grid points computing Jacobian terms for diffusion and advection
 at each grid point
 */
  for (PetscInt xi = xs; xi < xs + xm; xi++) 
  {
// Boundary conditions
// Everything to the left of the surface is empty
   if (xi <= surfacePosition || xi == xSize - 1) continue;

// Set the grid position
   gridPosition[0] = grid[xi];

// Get the temperature from the temperature handler
   auto temperature = temperatureHandler->getTemperature(gridPosition, ftime);

// Update the network if the temperature changed
   if (!xolotlCore::equal(temperature, lastTemperature)) 
   {
    network->setTemperature(temperature);
// Update the modified trap-mutation rate
// that depends on the network reaction rates
    mutationHandler->updateTrapMutationRate(network);
    lastTemperature = temperature;
   }

// Get the partial derivatives for the diffusion
   diffusionHandler->computePartialsForDiffusion( network, diffVals,
                                                  diffIndices, 
                                                  grid[xi] - grid[xi - 1], 
                                                  grid[xi + 1] - grid[xi], xi);

// Loop on the number of diffusion cluster to set the values in the Jacobian
   for (int i = 0; i < nDiff; i++) 
   {
// Set grid coordinate and component number for the row
    row.i = xi;
    row.c = diffIndices[i];

// Set grid coordinates and component numbers for the columns
// corresponding to the middle, left, and right grid points
    cols[0].i = xi; // middle
    cols[0].c = diffIndices[i];
    cols[1].i = xi - 1; // left
    cols[1].c = diffIndices[i];
    cols[2].i = xi + 1; // right
    cols[2].c = diffIndices[i];

    ierr = MatSetValuesStencil(J, 1, &row, 3, cols, diffVals + (3 * i),
                               ADD_VALUES);
    checkPetscError(ierr, "PetscSolver1DHandler::computeOffDiagonalJacobian: "
                          "MatSetValuesStencil (diffusion) failed.");
   }

// Get the partial derivatives for the advection
   for (int l = 0; l < advectionHandlers.size(); l++) 
   {
    advectionHandlers[l]->computePartialsForAdvection(network, advecVals, 
                                                      advecIndices, gridPosition,
                                                      grid[xi] - grid[xi - 1], 
                                                      grid[xi + 1] - grid[xi], xi);

// Get the stencil indices to know where to put the partial derivatives in the Jacobian
    auto advecStencil = advectionHandlers[l]->getStencilForAdvection( gridPosition );

// Get the number of advecting clusters
    nAdvec = advectionHandlers[l]->getNumberOfAdvecting();

// Loop on the number of advecting cluster to set the values in the Jacobian
    for (int i = 0; i < nAdvec; i++) 
    {
// Set grid coordinate and component number for the row
     row.i = xi;
     row.c = advecIndices[i];

// If we are on the sink, the partial derivatives are not the same
// Both sides are giving their concentrations to the center
     if (advectionHandlers[l]->isPointOnSink(gridPosition)) 
     {
      cols[0].i = xi - advecStencil[0]; // left?
      cols[0].c = advecIndices[i];
      cols[1].i = xi + advecStencil[0]; // right?
      cols[1].c = advecIndices[i];
     } 
     else 
     {
// Set grid coordinates and component numbers for the columns
// corresponding to the middle and other grid points
      cols[0].i = xi; // middle
      cols[0].c = advecIndices[i];
      cols[1].i = xi + advecStencil[0]; // left or right
      cols[1].c = advecIndices[i];
     }

// Update the matrix
     ierr = MatSetValuesStencil(J, 1, &row, 2, cols,
                                advecVals + (2 * i), ADD_VALUES);
     checkPetscError(ierr, "PetscSolver1DHandler::computeOffDiagonalJacobian: "
                           "MatSetValuesStencil (advection) failed.");
    }
   }
  }
  return;
 }

//--------------------------------------------------------------------------------
 void PetscSolver1DHandler::computeDiagonalJacobian(TS& ts, Vec& localC,
                                                    Mat& J, PetscReal ftime) 
 {
  PetscErrorCode ierr;

// Get the distributed array
  DM da;
  ierr = TSGetDM(ts, &da);
  checkPetscError(ierr, "PetscSolver1DHandler::computeDiagonalJacobian: "
                        "TSGetDM failed.");

// Get the total size of the grid for the boundary conditions
  int xSize = grid.size();

// Get pointers to vector data
  PetscScalar **concs = nullptr;
  ierr = DMDAVecGetArrayDOFRead(da, localC, &concs);
  checkPetscError(ierr, "PetscSolver1DHandler::computeDiagonalJacobian: "
                        "DMDAVecGetArrayDOFRead failed.");

// Get local grid boundaries
  PetscInt xs, xm;
  ierr = DMDAGetCorners(da, &xs, NULL, NULL, &xm, NULL, NULL);
  checkPetscError(ierr, "PetscSolver1DHandler::computeDiagonalJacobian: "
                        "DMDAGetCorners failed.");

// Pointer to the concentrations at a given grid point
  PetscScalar *concOffset = nullptr;

// Degrees of freedom is the total number of clusters in the network
  const int dof = network->getDOF();

// Get all the He clusters in the network
  auto heliums = network->getAll(xolotlCore::heType);

// Compute the total concentration of atoms contained in bubbles
  double atomConc = 0.0;

// Loop over grid points to get the atom concentration
// near the surface
  for (int xi = xs; xi < xs + xm; xi++) 
  {
// Boundary conditions
   if (xi <= surfacePosition || xi == xSize - 1) continue;

// We are only interested in the helium near the surface
   if (grid[xi] - grid[surfacePosition] > 2.0) continue;

// Get the concentrations at this grid point
   concOffset = concs[xi];
// Copy data into the PSIClusterReactionNetwork
   network->updateConcentrationsFromArray(concOffset);

// Sum the total atom concentration
   atomConc += network->getTotalTrappedAtomConcentration() * (grid[xi] - grid[xi - 1]);
  }

// Share the concentration with all the processes
  double totalAtomConc = 0.0;
  MPI_Allreduce(&atomConc, &totalAtomConc, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);

// Set the disappearing rate in the modified TM handler
  mutationHandler->updateDisappearingRate(totalAtomConc);

// Arguments for MatSetValuesStencil called below
  MatStencil rowId;
  MatStencil colIds[dof];
  int pdColIdsVectorSize = 0;
  PetscScalar *reactionVals;
  reactionVals = new PetscScalar[dof * dof];
  PetscInt *reactionIndices;
  reactionIndices = new PetscInt[dof * dof];
  PetscInt reactionSize[dof];

// Store the total number of He clusters in the network for the
// modified trap-mutation
  int nHelium = heliums.size();

// Declarations for variables used in the loop
  std::vector<double> gridPosition;
  gridPosition.push_back(0.0);
  gridPosition.push_back(0.0);
  gridPosition.push_back(0.0);

// Loop over the grid points
  for (PetscInt xi = xs; xi < xs + xm; xi++) 
  {
// Boundary conditions
// Everything to the left of the surface is empty
   if (xi <= surfacePosition || xi == xSize - 1) continue;

// Set the grid position
   gridPosition[0] = grid[xi];

// Get the temperature from the temperature handler
   auto temperature = temperatureHandler->getTemperature(gridPosition, ftime);

// Update the network if the temperature changed
   if (!xolotlCore::equal(temperature, lastTemperature)) 
   {
    network->setTemperature(temperature);
// Update the modified trap-mutation rate
// that depends on the network reaction rates
    mutationHandler->updateTrapMutationRate(network);
    lastTemperature = temperature;
   }

// Copy data into the ReactionNetwork so that it can
// compute the new concentrations.
   concOffset = concs[xi];
   network->updateConcentrationsFromArray(concOffset);

// ----- Take care of the reactions for all the reactants -----

// Compute all the partial derivatives for the reactions
   network->computeAllPartials(reactionVals, reactionIndices, reactionSize);

// Update the column in the Jacobian that represents each DOF
   for (int i = 0; i < dof; i++) 
   {
// Set grid coordinate and component number for the row
    rowId.i = xi;
    rowId.c = i;

// Number of partial derivatives
    pdColIdsVectorSize = reactionSize[i];
// Loop over the list of column ids
    for (int j = 0; j < pdColIdsVectorSize; j++) 
    {
// Set grid coordinate and component number for a column in the list
     colIds[j].i = xi;
     colIds[j].c = reactionIndices[i * dof + j];
// Get the partial derivative from the array of all of the partials
     reactingPartialsForCluster[j] = reactionVals[i * dof + j];
    }
// Update the matrix
    ierr = MatSetValuesStencil(J, 1, &rowId, pdColIdsVectorSize, colIds,
                               reactingPartialsForCluster.data(), ADD_VALUES);
    checkPetscError(ierr, "PetscSolver1DHandler::computeDiagonalJacobian: "
                          "MatSetValuesStencil (reactions) failed.");
   }

// ----- Take care of the modified trap-mutation for all the reactants -----

// Arguments for MatSetValuesStencil called below
   MatStencil row, col;
   PetscScalar mutationVals[3 * nHelium];
   PetscInt mutationIndices[3 * nHelium];

// Compute the partial derivative from modified trap-mutation at this grid point
   int nMutating = mutationHandler->computePartialsForTrapMutation( network,
                                                 mutationVals, mutationIndices, xi);

// Loop on the number of helium undergoing trap-mutation to set the values
// in the Jacobian
   for (int i = 0; i < nMutating; i++) 
   {
// Set grid coordinate and component number for the row and column
// corresponding to the helium cluster
    row.i = xi;
    row.c = mutationIndices[3 * i];
    col.i = xi;
    col.c = mutationIndices[3 * i];

    ierr = MatSetValuesStencil(J, 1, &row, 1, &col,
                               mutationVals + (3 * i), ADD_VALUES);
    checkPetscError(ierr, "PetscSolver1DHandler::computeDiagonalJacobian: "
                          "MatSetValuesStencil (He trap-mutation) failed.");

// Set component number for the row
// corresponding to the HeV cluster created through trap-mutation
    row.c = mutationIndices[(3 * i) + 1];

    ierr = MatSetValuesStencil(J, 1, &row, 1, &col,
                               mutationVals + (3 * i) + 1, ADD_VALUES);
    checkPetscError(ierr, "PetscSolver1DHandler::computeDiagonalJacobian: "
                          "MatSetValuesStencil (HeV trap-mutation) failed.");

// Set component number for the row
// corresponding to the interstitial created through trap-mutation
    row.c = mutationIndices[(3 * i) + 2];

    ierr = MatSetValuesStencil(J, 1, &row, 1, &col,
                               mutationVals + (3 * i) + 2, ADD_VALUES);
    checkPetscError(ierr, "PetscSolver1DHandler::computeDiagonalJacobian: "
                          "MatSetValuesStencil (I trap-mutation) failed.");
   }

  }

/*
 Restore vectors
 */
  ierr = DMDAVecRestoreArrayDOFRead(da, localC, &concs);
  checkPetscError(ierr, "PetscSolver1DHandler::computeDiagonalJacobian: "
                        "DMDAVecRestoreArrayDOFRead failed.");
  ierr = DMRestoreLocalVector(da, &localC);
  checkPetscError(ierr, "PetscSolver1DHandler::computeDiagonalJacobian: "
                        "DMRestoreLocalVector failed.");

// Delete arrays
  delete[] reactionVals;
  delete[] reactionIndices;

  return;

 }

} /* end namespace xolotlSolver */
