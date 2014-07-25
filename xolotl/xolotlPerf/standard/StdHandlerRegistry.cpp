#include <iostream>
#include <cassert>
#include "mpi.h"
#include <unistd.h>
#include <float.h>
#include <math.h>
#include "xolotlPerf/standard/StdHandlerRegistry.h"
#include "xolotlPerf/standard/EventCounter.h"


namespace xolotlPerf
{

StdHandlerRegistry::StdHandlerRegistry(void)
{
    // nothing else to do
}


StdHandlerRegistry::~StdHandlerRegistry(void)
{
    // Release the objects we have been tracking.
    // Because we use shared_ptrs for these objects,
    // we do not need to explicitly delete the objects themselves.
    allTimers.clear();
    allEventCounters.clear();
    allHWCounterSets.clear();
}


// We can create the EventCounters, since they don't depend on
// more specialized functionality from any of our subclasses.
std::shared_ptr<IEventCounter>
StdHandlerRegistry::getEventCounter(std::string name)
{
    // TODO - associate the object we create with the current region
    std::shared_ptr<IEventCounter> ret;

    // Check if we have already created an event counter with this name.
    auto iter = allEventCounters.find(name);
    if( iter != allEventCounters.end() )
    {
        // We have already created an event counter with this name.
        // Return name.
        ret = iter->second;
    }
    else
    {
        // We have not yet created an event counter with this name.
        // Build one and keep track of it.
        ret = std::make_shared<EventCounter>(name);
        allEventCounters[name] = ret;
    }
    return ret;
}


template<class T>
void
StdHandlerRegistry::PerfObjStatistics<T>::outputTo(std::ostream& os) const
{
    os << "  " << name << '\n'
        << "    " << "process_count: " << processCount << '\n'
        << "    " << "min: " << min << '\n'
        << "    " << "max: " << max << '\n'
        << "    " << "average: " << average << '\n'
        << "    " << "stdev: " << stdev << '\n'
        << std::endl;
}


template<typename T>
void
StdHandlerRegistry::CollectObjectNames( int myRank,
    const std::vector<std::string>& myNames,
    std::map<std::string, PerfObjStatistics<T> >& stats ) const
{
    // Determine amount of space required for names
    unsigned int nBytes = 0;
    for( auto nameIter = myNames.begin(); nameIter != myNames.end(); ++nameIter )
    {
        // Add enough space for the name plus a NUL terminating character.
        nBytes += (nameIter->length() + 1);
    }
    
    // Let root know how much space it needs to collect all object names
    unsigned int totalNumBytes = 0;
    MPI_Reduce( &nBytes, &totalNumBytes, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD );

    // Marshal all our object names.
    char* myNamesBuf = new char[nBytes];
    char* pName = myNamesBuf;
    for( auto nameIter = myNames.begin(); nameIter != myNames.end(); ++nameIter )
    {
        strcpy( pName, nameIter->c_str() );
        pName += (nameIter->length() + 1);   // skip the NUL terminator
    }
    assert( pName == (myNamesBuf + nBytes) );

    // Provide all names to root.
    // First, provide the amount of data from each process.
    int cwSize;
    MPI_Comm_size( MPI_COMM_WORLD, &cwSize );
    char* allNames = (myRank == 0) ? new char[totalNumBytes] : NULL;
    int* allNameCounts = (myRank == 0) ? new int[cwSize] : NULL;
    int* allNameDispls = (myRank == 0) ? new int[cwSize] : NULL;

    MPI_Gather( &nBytes, 1, MPI_INT, allNameCounts, 1, MPI_INT, 0, MPI_COMM_WORLD );

    // Next, root computes the displacements for data from each process.
    if( myRank == 0 )
    {
        allNameDispls[0] = 0;
        for( unsigned int i = 1; i < cwSize; ++i )
        {
            allNameDispls[i] = allNameDispls[i-1] + allNameCounts[i-1];
        }
    }

    // Finally, gather all names to the root process.
    MPI_Gatherv( myNamesBuf, 
                    nBytes, 
                    MPI_CHAR,
                    allNames,
                    allNameCounts,
                    allNameDispls,
                    MPI_CHAR,
                    0,
                    MPI_COMM_WORLD );

    if( myRank == 0 )
    {
        // Process the gathered names to determine the 
        // set of all known object names.
        pName = allNames;
        while( pName < (allNames + totalNumBytes) )
        {
            auto iter = stats.find(pName);
            if( iter == stats.end() )
            {
                // This is an object  name we have not seen before.
                // Add it to the statistics map.
                stats.insert( std::pair<std::string, PerfObjStatistics<T> >(pName, PerfObjStatistics<T>(pName) ) );
            }
            
            // Advance to next object name
            pName += (strlen(pName) + 1);
        }
        assert( pName == allNames + totalNumBytes );
    }

    // clean up
    delete[] myNamesBuf;
    delete[] allNames;
    delete[] allNameCounts;
    delete[] allNameDispls;
}


template<typename T>
void
StdHandlerRegistry::AggregateStatistics( int myRank,
    const std::map<std::string, std::shared_ptr<T> >& allObjs,
    std::map<std::string, PerfObjStatistics<typename T::ValType> >& stats ) const
{
    // Determine the set of object names known across all processes.
    // Since some processes may define an object that others don't, we
    // have to form the union across all processes.
    // Unfortunately, because the strings are of different lengths,
    // we have a more difficult marshal/unmarshal problem than we'd like.
    std::vector<std::string> objNames;
    for( auto oiter = allObjs.begin(); oiter != allObjs.end(); ++oiter )
    {
        objNames.push_back(oiter->first);
    }
    CollectObjectNames<typename T::ValType>( myRank, objNames, stats );

    // Now collect statistics for each object the program defined.
    int nObjs;
    if( myRank == 0 )
    {
        nObjs = stats.size();
    }
    MPI_Bcast( &nObjs, 1, MPI_INT, 0, MPI_COMM_WORLD );
    assert( nObjs >= 0 );

    auto tsiter = stats.begin();
    for( unsigned int idx = 0; idx < nObjs; ++idx )
    {
        // broadcast the current object's name
        int nameLen = (myRank == 0) ? tsiter->second.name.length() : -1;
        MPI_Bcast( &nameLen, 1, MPI_INT, 0, MPI_COMM_WORLD );
        // we can safely cast away const on the tsiter data string because
        // the only process that accesses that string is rank 0,
        // and it only reads the data.
        char* objName = (myRank == 0) ? const_cast<char*>(tsiter->second.name.c_str()) : new char[nameLen+1];
        MPI_Bcast( objName, nameLen+1, MPI_CHAR, 0, MPI_COMM_WORLD );

        // do we know about the current object?
        auto currObjIter = allObjs.find(objName);
        int knowObject = (currObjIter != allObjs.end()) ? 1 : 0;

        // collect count of processes knowing the current object
        unsigned int* pcount = (myRank == 0) ? &(tsiter->second.processCount) : NULL;
        MPI_Reduce(&knowObject, pcount, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

        // collect min value of current object
        typename T::ValType* pMinVal = (myRank == 0) ? &(tsiter->second.min) : NULL;
        typename T::ValType myVal = knowObject ? currObjIter->second->getValue() : DBL_MAX;
        MPI_Reduce(&myVal, pMinVal, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

        // collect max value of current object
        typename T::ValType* pMaxVal = (myRank == 0) ? &(tsiter->second.max) : NULL;
        myVal = knowObject ? currObjIter->second->getValue() : 0.0;
        MPI_Reduce(&myVal, pMaxVal, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

        // collect sum of current object's values (for computing avg and stdev)
        double valSum;
        // use the same myVal as for max: actual value if known, 0 otherwise
        MPI_Reduce(&myVal, &valSum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if( myRank == 0 )
        {
            tsiter->second.average = valSum / tsiter->second.processCount;
        }

        // collect sum of squares of current object's values (for stdev)
        double valSquaredSum;
        double myValSquared = myVal*myVal;
        MPI_Reduce(&myValSquared, &valSquaredSum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if( myRank == 0 )
        {
            tsiter->second.stdev = sqrt( (valSquaredSum / tsiter->second.processCount) - (tsiter->second.average * tsiter->second.average) );
        }

        // clean up
        if( myRank != 0 )
        {
            delete[] objName;
        }

        // advance to next object
        if( myRank == 0 )
        {
            ++tsiter;
        }
    }
}




void
StdHandlerRegistry::reportStatistics(std::ostream& os) const
{
    int myRank;
    MPI_Comm_rank( MPI_COMM_WORLD, &myRank );

    // Compute statistics about performance data collected by all processes.
    std::map<std::string, PerfObjStatistics<ITimer::ValType> > timerStats;
    AggregateStatistics<ITimer>( myRank, allTimers, timerStats );

    std::map<std::string, PerfObjStatistics<IEventCounter::ValType> > counterStats;
    AggregateStatistics<IEventCounter>( myRank, allEventCounters, counterStats );

    std::map<std::string, PerfObjStatistics<IHardwareCounter::CounterType> > hwCounterStats;

    // If I am the rank 0 process, output statistics on the given stream.
    if( myRank == 0 )
    {
        os << "\nTimers:\n";
        for( auto iter = timerStats.begin(); iter != timerStats.end(); ++iter )
        {
            iter->second.outputTo(os);
        }

        os << "\nCounters:\n";
        for( auto iter = counterStats.begin(); iter != counterStats.end(); ++iter )
        {
            iter->second.outputTo(os);
        }

        os << "\nHardwareCounters:\n";
        for( auto iter = hwCounterStats.begin(); iter != hwCounterStats.end(); ++iter )
        {
            iter->second.outputTo(os);
        }
    }
}


} // namespace xolotlPerf

