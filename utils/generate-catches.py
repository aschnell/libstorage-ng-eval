#!/usr/bin/python3


# Program to generate the catches file for the swig generated bindings.


import networkx
import utils


def output_exception_classes():

    exception_classnames = networkx.descendants(utils.classes, "storage::Exception")
    exception_classnames.add("storage::Exception")

    for classname in sorted(exception_classnames):
        print("%exceptionclass " + classname + ";")

    print("")


def output_functions(functions):

    for function in sorted(functions, key=lambda function: function.name):
        if function.exception_names:
            print("%%catches(%s) %s%s;" % (", ".join(name for name in function.exception_names),
                                           function.name, function.args_string))


def output_global_functions():

    output_functions(utils.functions)

    print("")


def output_class_functions():

    for classname in sorted(utils.classes.nodes()):
        output_functions(utils.classes.nodes[classname].get('functions', []))

    print("")


print("""
// This file is generated by utils/generate-catches.py.
""")

output_exception_classes()
output_global_functions()
output_class_functions()
