#ifndef TVP_WEB_HOST_ARGUMENTS_H
#define TVP_WEB_HOST_ARGUMENTS_H

#include <string>
#include <vector>

// Raw Web main arguments have a separate lifetime from the engine's option
// stock, which is rebuilt while the data path and config priorities are set.
const std::vector<std::string> &TVPGetWebCommandLineArguments();

#endif
