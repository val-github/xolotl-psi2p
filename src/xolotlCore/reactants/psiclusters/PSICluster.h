#ifndef PSICLUSTER_H
#define PSICLUSTER_H

// Includes
#include <Reactant.h>

namespace xolotlPerf 
{
 class ITimer;
}

namespace xolotlCore 
{

/**
 * The PSICluster class is a Reactant that is specialized to work for
 * simulations of plasma-surface interactions. It provides special routines
 * for calculating the total flux due to production and dissociation and
 * obtaining the cluster size.
 *
 * PSIClusters must always be initialized with a size. If the constructor is
 * passed a size of zero or less, the actual size will be set to 1.
 *
 * The getComposition() operation is implemented by subclasses and will always
 * return a map with the keys He, V, I, HeV or HeI. The operation getTypeName()
 * will always return one of the same values.
 *
 * As a rule, it is possible to access directly some of the private members of
 * this class (id, concentration, reactionRadius, diffusionCoefficient, size,
 * typeName) instead of using the "get" functions for performance reasons. In
 * order to change these values the "set" functions must still be used.
 */
class PSICluster: public Reactant {

protected:

	/**
	 * This is a protected class that is used to implement the flux calculations
	 * for two body reactions or dissociation.
	 *
	 * The constant k+ or k- is stored along the clusters taking part in the
	 * reaction or dissociation for faster computation because they only change
	 * when the temperature change. k is computed when setTemperature() is called.
	 */
	class ClusterPair {
	public:

		/**
		 * The first cluster in the pair
		 */
		PSICluster * first;

		/**
		 * The second cluster in the pair
		 */
		PSICluster * second;

		/**
		 * The first cluster helium distance in the group (0.0 for non-super clusters)
		 */
		double firstHeDistance;

		/**
		 * The first cluster vacancy distance in the group (0.0 for non-super clusters)
		 */
		double firstVDistance;

		/**
		 * The second cluster helium distance in the group (0.0 for non-super clusters)
		 */
		double secondHeDistance;

		/**
		 * The second cluster vacancy distance in the group (0.0 for non-super clusters)
		 */
		double secondVDistance;

		/**
		 * The reaction/dissociation constant associated to this
		 * reaction or dissociation
		 */
		double kConstant;

		//! The constructor
		ClusterPair(PSICluster * firstPtr, PSICluster * secondPtr, double k)
		: first(firstPtr), second(secondPtr), kConstant(k), firstHeDistance(0.0), firstVDistance(0.0),
		  secondHeDistance(0.0), secondVDistance(0.0) {}
	};

	/**
	 * This is a protected class that is used to implement the flux calculations
	 * for combinations.
	 *
	 * The constant k+ is stored along the cluster that combines with this cluster
	 * for faster computation because they only change when the temperature change.
	 * k+ is computed when setTemperature() is called.
	 */
	class CombiningCluster {
	public:

		/**
		 * The combining cluster
		 */
		PSICluster* combining;

		/**
		 * The combining cluster helium distance in the group (0.0 for non-super clusters)
		 */
		double heDistance;

		/**
		 * The combining cluster vacancy distance in the group (0.0 for non-super clusters)
		 */
		double vDistance;

		/**
		 * The reaction constant associated to this reaction
		 */
		double kConstant;

		//! The constructor
		CombiningCluster(PSICluster * Ptr, double k)
		: combining(Ptr), kConstant(k), heDistance(0.0), vDistance(0.0) {}
	};

	/**
	 * Computes a row (or column) of the reaction connectivity matrix
	 * corresponding to this cluster.
	 *
	 * Connections are made between this cluster and any clusters it
	 * affects in combination and production reactions.
	 *
	 * The base-class implementation handles the common part of single species clusters.
	 *
	 * A_(x-i) + A_i --> A_x
	 *
	 * Must be overridden by subclasses.
	 */
	virtual void createReactionConnectivity();

	/**
	 * Computes a row (or column) of the dissociation connectivity matrix
	 * corresponding to this cluster.
	 *
	 * Connections are made between this cluster and any clusters it affects
	 * in a dissociation reaction.
	 *
	 * The base-class implementation handles dissociation for regular clusters
	 * by processing the reaction
	 *
	 * A_x --> A_(x-1) + A
	 *
	 * Must be overridden by subclasses.
	 *
	 */
	virtual void createDissociationConnectivity();

	/**
	 * Calculate the dissociation constant of the first cluster with respect to
	 * the single-species cluster of the same type based on the current clusters
	 * atomic volume, reaction rate constant, and binding energies.
	 *
	 * @param dissociatingCluster The cluster that is dissociating, it is its binding
	 * energy that must be used
	 * @param singleCluster One of the clusters that dissociated from the parent,
	 * must be the single size one in order to select the right type of binding energy
	 * @param secondCluster The second cluster that dissociated from the parent
	 * @return The dissociation constant
	 */
	double calculateDissociationConstant(const PSICluster & dissociatingCluster,
			const PSICluster & singleCluster, const PSICluster & secondCluster) const;
			
	/**
	 * This operation adds the dissociating cluster to the list of dissociatingPairs.
	 * It is called by createDissociationConnectivity to process the reaction.
	 *
	 * @param dissociatingCluster The cluster that creates this cluster
	 * by dissociation
	 * @param emittedCluster The cluster that is also emitted during the
	 * dissociation
	 */
	void dissociateCluster(PSICluster * dissociatingCluster, PSICluster * emittedCluster);

	/**
	 * This operation creates the two emitted clusters from the dissociation of
	 * this cluster. It is called by createDissociationConnectivity to process the
	 * reaction and handle the connectivity.
	 *
	 * @param firstEmittedCluster The first cluster emitted by the
	 * dissociation. Should be the single size one to have correct
	 * computation of the dissociation constant
	 * @param secondEmittedCluster The second cluster emitted by the
	 * dissociation
	 */
	void emitClusters(
			PSICluster * firstEmittedCluster,
			PSICluster * secondEmittedCluster);

	/**
	 * This operation "combines" clusters in the sense that it handles all of
	 * the logic and caching required to correctly process the reaction
	 *
	 * A_x + A_y --> A_(x+y)
	 *
	 * or
	 *
	 * A_x + B_y --> (A_x)(B_y)
	 *
	 * or
	 *
	 * (A_x)(B_y) + B_z --> (A_x)[B_(z+y)]
	 *
	 * for each cluster in the set that interacts with this cluster.
	 *
	 * This operation fills the reaction connectivity array as well as the
	 * array of combining clusters.
	 *
	 * @param clusters The clusters that can combine with this cluster
	 * @param productName The name of the product produced in the reaction
	 */
	virtual void combineClusters(std::vector<IReactant *> & clusters,
			const std::string& productName);

	/**
	 * This operation handles partial replacement reactions of the form
	 *
	 * (A_x)(B_y) + C_z --> (A_x)[B_(y-z)]
	 *
	 * for each compound cluster in the set.
	 *
	 * This operation fills the reaction connectivity array as well as the
	 * array of combining clusters.
	 *
	 * Works only if "this" is not a mixed species. Needs to be overridden
	 * for the other ones.
	 *
	 * @param clusters The clusters that have part of their B components
	 * replaced. It is assumed that each element of this set represents a
	 * cluster of the form (A_x)(B_y).
	 * @param oldComponentName The name of the component that will be partially
	 * replaced
	 */
	virtual void replaceInCompound(std::vector<IReactant *> & clusters,
			const std::string& oldComponentName);

	/** This operation handles reactions where interstitials fill vacancies,
	 * sometimes referred to vacancy-interstitial annihilation. The reaction
	 * is of the form
	 *
	 * I_a + V_b
	 * --> I_(a-b), if a > b
	 * --> V_(b-a), if a < b
	 * --> 0, if a = b
	 *
	 * It is important to note that I_a + V_b = V_b + I_a.
	 *
	 * The operation assumes "this" is the first cluster in the reaction and
	 * relies on the caller to specify the second cluster name/type.
	 *
	 * This operation fills the reaction connectivity array as well as the
	 * array of combining clusters.
	 *
	 * This operation also I_a and V_b as a reacting pair of the product. It
	 * is simpler and cheaper (O(C)) to do this operation here and quite
	 * computationally difficult by comparison (O(numI*numV)) to do this
	 * operation on the child since it has to search all of the possible
	 * parents.
	 *
	 * @param clusters The set of clusters of the second type that interact
	 * with this cluster
	 **/
	void fillVWithI(std::vector<IReactant *> & clusters);

	/**

	 * This operation returns a set that contains only the entries of the
	 * reaction connectivity array that are non-zero.
	 *
	 * @return The set of connected reactants. Each entry in the set is the id
	 * of a connected cluster for forward reactions.
	 */
	std::set<int> getReactionConnectivitySet() const;

	/**
	 * This operation returns a set that contains only the entries of the
	 * dissociation connectivity array that are non-zero.
	 *
	 * @return The set of connected reactants. Each entry in the set is the id
	 * of a connected cluster for dissociation reactions
	 */
	const std::set<int> & getDissociationConnectivitySet() const;

	/**
	 * The default constructor is protected
	 */
	PSICluster();

public:

	/**
	 * A vector of ClusterPairs that represents reacting pairs of clusters
	 * that produce this cluster. This vector should be populated early in the
	 * cluster's lifecycle by subclasses. In the standard Xolotl clusters,
	 * this vector is filled in createReactionConnectivity.
	 */
	std::vector<ClusterPair> reactingPairs;

	/**
	 * A vector of pointers to ClusterPairs that represents the effective reacting
	 * pairs, i.e. those for which the reaction rate is not 0.0. Should be filled
	 * every time the temperature changes.
	 */
	std::vector<ClusterPair *> effReactingPairs;

	/**
	 * A vector of clusters that combine with this cluster to produce other
	 * clusters. This vector should be populated early in the cluster's
	 * lifecycle by subclasses. In the standard Xolotl clusters, this vector is
	 * filled in createReactionConnectivity.
	 */
	std::vector<CombiningCluster> combiningReactants;

	/**
	 * A vector of pointers to CombiningCluster that represents the effective
	 * combining clusters, i.e. those for which the reaction rate is not 0.0.
	 * Should be filled every time the temperature changes.
	 */
	std::vector<CombiningCluster *> effCombiningReactants;

	/**
	 * A vector of pairs of clusters: the first one is the one dissociation into
	 * this cluster, the second one is the one that is emitted at the same time
	 * during the dissociation. This vector should be populated early in the
	 * cluster's lifecycle by subclasses. In the standard Xolotl clusters, this
	 * vector is filled in dissociateCluster that is called by
	 * createDissociationConnectivity.
	 */
	std::vector<ClusterPair> dissociatingPairs;

	/**
	 * A vector of pointers to ClusterPairs that represents the effective dissociating
	 * pairs, i.e. those for which the dissociation rate is not 0.0. Should be filled
	 * every time the temperature changes.
	 */
	std::vector<ClusterPair *> effDissociatingPairs;

	/**
	 * A vector of ClusterPairs that represent pairs of clusters that are emitted
	 * from the dissociation of this cluster. This vector should be populated early
	 * in the cluster's lifecycle by subclasses. In the standard Xolotl clusters,
	 * this vector is filled in emitClusters that is called by
	 * createDissociationConnectivity.
	 */
	std::vector<ClusterPair> emissionPairs;

	/**
	 * A vector of pointers to ClusterPairs that represents the effective emission
	 * pairs, i.e. those for which the dissociation rate is not 0.0. Should be filled
	 * every time the temperature changes.
	 */
	std::vector<ClusterPair *> effEmissionPairs;

	/**
	 * The default constructor
	 *
	 * @param registry The performance handler registry
	 */
	PSICluster(std::shared_ptr<xolotlPerf::IHandlerRegistry> registry);

	/**
	 * The copy constructor
	 *
	 * @param other The cluster to copy
	 */
	PSICluster(PSICluster &other);

	/**
	 * The destructor
	 */
	virtual ~PSICluster() {}

	/**
	 * Returns a reactant created using the copy constructor
	 */
	virtual std::shared_ptr<IReactant> clone() {
		return std::shared_ptr<IReactant> (new PSICluster(*this));
	}

	/**
	 * Sets the collection of other clusters that make up
	 * the reaction network in which this cluster exists.
	 *
	 * @param network The reaction network of which this cluster is a part
	 */
	void setReactionNetwork(
			const std::shared_ptr<IReactionNetwork> reactionNetwork);

	/**
	 * This operation returns the connectivity array for this cluster for
	 * forward reactions. An entry with value one means that this cluster
	 * and the cluster with id = index + 1 are connected.
	 * 
	 * @return The connectivity array for "forward" (non-dissociating)
	 * reactions
	 */
	virtual std::vector<int> getReactionConnectivity() const;

	/**
	 * This operation returns the connectivity array for this cluster for
	 * forward reactions. An entry with value one means that this cluster
	 * and the cluster with id = index + 1 are connected.
	 * 
	 * @return The connectivity array for "forward" (non-dissociating)
	 * reactions
	 */
	virtual std::vector<int> getDissociationConnectivity() const;

	/**
	 * This operation returns the first helium momentum.
	 *
	 * @return The momentum
	 */
	virtual double getHeMomentum() const;

	/**
	 * This operation returns the first vacancy momentum.
	 *
	 * @return The momentum
	 */
	virtual double getVMomentum() const;

	/**
	 * This operation returns the total flux of this cluster in the
	 * current network.
	 *
	 * @return The total change in flux for this cluster due to all
	 * reactions
	 */
	virtual double getTotalFlux();

	/**
	 * This operation returns the total change in this cluster due to
	 * other clusters dissociating into it.
	 *
	 * @return The flux due to dissociation of other clusters
	 */
	virtual double getDissociationFlux() const;

	/**
	 * This operation returns the total change in this cluster due its
	 * own dissociation.
	 *
	 * @return The flux due to its dissociation
	 */
	virtual double getEmissionFlux() const;

	/**
	 * This operation returns the total change in this cluster due to
	 * the production of this cluster by other clusters.
	 *
	 * @return The flux due to this cluster being produced
	 */
	virtual double getProductionFlux() const;

	/**
	 * This operation returns the total change in this cluster due to
	 * the combination of this cluster with others.
	 *
	 * @return The flux due to this cluster combining with other clusters
	 */
	virtual double getCombinationFlux() const;

	/**
	 * This operation returns the list of partial derivatives of this cluster
	 * with respect to all other clusters in the network. The combined lists
	 * of partial derivatives from all of the clusters in the network can be
	 * used to form, for example, a Jacobian.
	 *
	 * @return The partial derivatives for this cluster where index zero
	 * corresponds to the first cluster in the list returned by the
	 * ReactionNetwork::getAll() operation.
	 */
	virtual std::vector<double> getPartialDerivatives() const;

	/**
	 * This operation works as getPartialDerivatives above, but instead of
	 * returning a vector that it creates it fills a vector that is passed to
	 * it by the caller. This allows the caller to optimize the amount of
	 * memory allocations to just one if they are accessing the partial
	 * derivatives many times.
	 *
	 * @param the vector that should be filled with the partial derivatives
	 * for this reactant where index zero corresponds to the first reactant in
	 * the list returned by the ReactionNetwork::getAll() operation. The size of
	 * the vector should be equal to ReactionNetwork::size().
	 *
	 */
	virtual void getPartialDerivatives(std::vector<double> & partials) const;

	/**
	 * This operation computes the partial derivatives due to production
	 * reactions.
	 *
	 * @param partials The vector into which the partial derivatives should be
	 * inserted. This vector should have a length equal to the size of the
	 * network.
	 */
	virtual void getProductionPartialDerivatives(std::vector<double> & partials) const;

	/**
	 * This operation computes the partial derivatives due to combination
	 * reactions.
	 *
	 * @param partials The vector into which the partial derivatives should be
	 * inserted. This vector should have a length equal to the size of the
	 * network.
	 */
	virtual void getCombinationPartialDerivatives(std::vector<double> & partials) const;

	/**
	 * This operation computes the partial derivatives due to dissociation of
	 * other clusters into this one.
	 *
	 * @param partials The vector into which the partial derivatives should be
	 * inserted. This vector should have a length equal to the size of the
	 * network.
	 */
	virtual void getDissociationPartialDerivatives(std::vector<double> & partials) const;

	/**
	 * This operation computes the partial derivatives due to emission
	 * reactions.
	 *
	 * @param partials The vector into which the partial derivatives should be
	 * inserted. This vector should have a length equal to the size of the
	 * network.
	 */
	virtual void getEmissionPartialDerivatives(std::vector<double> & partials) const;

	/**
	 * This operation reset the connectivity sets based on the information
	 * in the effective production and dissociation vectors.
	 */
	void resetConnectivities();

	/**
	 * This operation sets the diffusion factor, D_0, that is used to calculate
	 * the diffusion coefficient for this cluster.
	 *
	 * @param factor The diffusion factor
	 */
	void setDiffusionFactor(const double factor);

	/**
	 * This operation sets the migration energy for this reactant.
	 *
	 * @param energy The migration energy
	 */
	void setMigrationEnergy(const double energy);

	/**
	 * This operation returns the sum of combination rate and emission rate
	 * (where this cluster is on the left side of the reaction) for this
	 * particular cluster.
	 * This is used to computed the desorption rate in the
	 * modified trap-mutation handler.
	 *
	 * @return The rate
	 */
	double getLeftSideRate() const;

	/**
	 * This operation returns a list that represents the connectivity
	 * between this cluster and other clusters in the network.
	 * "Connectivity" indicates whether two clusters interact, via any
	 * mechanism, in an abstract sense (as if they were nodes connected by
	 * an edge on a network graph).
	 *
	 * @return An array of ones and zeros that indicate whether or not this
	 * cluster interacts via any mechanism with another cluster. A "1" at
	 * the i-th entry in this array indicates that the cluster interacts
	 * with the i-th cluster in the ReactionNetwork and a "0" indicates
	 * that it does not.
	 */
	std::vector<int> getConnectivity() const;

	/**
	 * Calculate all the rate constants for the reactions and dissociations in which this
	 * cluster is taking part. Store these values in the kConstant field of ClusterPair
	 * or CombiningCluster. Need to be called only at the beginning of the simulation.
	 */
	virtual void computeRateConstants();

	/**
	 * Update all the rate constants for the reactions and dissociations in which this
	 * cluster is taking part. Store these values in the kConstant field of ClusterPair
	 * or CombiningCluster. Need to be called when the temperature changes.
	 */
	virtual void updateRateConstants();

};

} /* end namespace xolotlCore */
#endif
