"""
This module provides classes and methods to launch the Labios application.
Labios is ....
"""
from jarvis_cd.basic.pkg import Application
from jarvis_util import *
from jarvis_util.shell.exec import Exec
from jarvis_util.shell.mpi_exec import MpiExecInfo

class Labios(Application):
    """
    This class provides methods to launch the Labios application.
    """
    def _init(self):
        """
        Initialize paths
        """
        pass

    def _configure_menu(self):
        """
        Create a CLI menu for the configurator method.
        For thorough documentation of these parameters, view:
        https://github.com/scs-lab/jarvis-util/wiki/3.-Argument-Parsing

        :return: List(dict)
        """
        return []

    def _configure(self, **kwargs):
        """
        Converts the Jarvis configuration to application-specific configuration.
        E.g., OrangeFS produces an orangefs.xml file.

        :param kwargs: Configuration parameters for this pkg.
        :return: None
        """
        pass

    def start(self):
        """
        Launch an application. E.g., OrangeFS will launch the servers, clients,
        and metadata services on all necessary pkgs.

        :return: None
        """
        mpi_info = MpiExecInfo(
            env=self.env,
            hostfile=self.jarvis.hostfile,
            nprocs=4,
            ppn=2 # Set to the number of processes you want
        )
        Exec('mpi_labios_test', mpi_info)

    def stop(self):
        """
        Stop a running application. E.g., OrangeFS will terminate the servers,
        clients, and metadata services.

        :return: None
        """
        pass

    def kill(self):
        """
        Forcibly a running application. E.g., OrangeFS will terminate the servers,
        clients, and metadata services.

        :return: None
        """
        pass

    def clean(self):
        """
        Destroy all data for an application. E.g., OrangeFS will delete all
        metadata and data directories in addition to the orangefs.xml file.

        :return: None
        """
        pass
