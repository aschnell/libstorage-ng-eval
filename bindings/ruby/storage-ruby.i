//
// Ruby SWIG interface definition for libstorage
//

%{
#include <sstream>
%}

%rename("empty?") "empty";

%rename("to_s") "get_displayname";

%rename("%(regex:/^(get_)(.*)/\\2/)s") "";
%rename("%(regex:/^(set_)(.*)/\\2=/)s") "";
%rename("%(regex:/^(is_)(.*)/\\2?/)s") "";

%rename("device_exists?") "device_exists";

%rename("exists_in_devicegraph?") "exists_in_devicegraph";
%rename("exists_in_probed?") "exists_in_probed";
%rename("exists_in_staging?") "exists_in_staging";
%rename("exists_in_system?") "exists_in_system";

%define use_ostream(CLASS)

%extend CLASS
{
    std::string to_s()
    {
	std::ostringstream out;
	out << *($self);
	return out.str();
    }
};

%enddef

%include "../storage.i"

// Tell SWIG to keep track of mappings between C/C++ structs/classes
// See Object Tracking section at http://www.swig.org/Doc1.3/Ruby.html#Ruby_nn60
%trackobjects;
