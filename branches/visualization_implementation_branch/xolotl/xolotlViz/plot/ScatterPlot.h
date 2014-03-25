#ifndef SCATTERPLOT_H
#define SCATTERPLOT_H

// Includes
#include "Plot.h"
#include "Point.h"
#include <vector>

namespace xolotlViz {

/**
 * Plot the data value as a function of one dimension. Available PlottingStyle are POINTS or LINE.
 * It can be associated to QvsXDataProvider, QvsYDataProvider, QvsZDataProvider, or QvsTimeDataProvider.
 */
class ScatterPlot: public Plot {

private:

	/**
	 * Vector containing two fields: one for the value and one for the direction as a function
	 * of which the value is plotted.
	 */
	std::vector< std::vector<double> > data;

public:

	/**
	 * The default constructor
	 */
	ScatterPlot();

	/**
	 * The destructor
	 */
	~ScatterPlot();

	/**
	 * Method managing everything that is related to the rendering of a plot.
	 */
	void render();

	/**
	 * Method returning the data points that are stored in the data vector.
	 */
	std::vector< std::vector<double> > getPoints() const;

	/**
	 * Method getting the X axis label with the help of the label provider.
	 */
	std::string getAxis1Label() const;

	/**
	 * Method getting the Y axis label with the help of the label provider.
	 */
	std::string getAxis2Label() const;

};

//end class ScatterPlot

} /* namespace xolotlViz */

#endif
