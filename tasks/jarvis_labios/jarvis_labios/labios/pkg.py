"""
This module provides classes and methods to launch the Labios application.
Labios is ....
"""
from jarvis_cd.basic.pkg import Application
from jarvis_util import *
from jarvis_util.shell.exec import Exec
from jarvis_util.shell.mpi_exec import MpiExecInfo
import os

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
        # Create necessary directories
        os.makedirs('/tmp/test_data', exist_ok=True)
        os.makedirs('/tmp/burst_buffer', exist_ok=True)
        os.makedirs('/tmp/pfs_storage', exist_ok=True)
        os.makedirs('/tmp/final_output', exist_ok=True)
        
        # Create config file
        config_path = '/home/rpawar4/grc-labios/tasks/build/labios/test/config.conf'
        with open(config_path, 'w') as f:
            f.write('# Basic Labios configuration\n')
            f.write('server_port = 6000\n')
            f.write('num_workers = 4\n')
            f.write('buffer_size = 1048576\n')

    def start(self):
        """
        Launch an application. E.g., OrangeFS will launch the servers, clients,
        and metadata services on all necessary pkgs.
        :return: None
        """
        # HACC test case command
        test_dir = '/home/rpawar4/grc-labios/tasks/build/labios/test'
        config_file = f'{test_dir}/config.conf'
        
        command = f'{test_dir}/montage_tabios {config_file} /tmp/test_data/ 1 /tmp/final_output/'
        print("=== Running Montage test case ===")
        print(f"Command: {command}")
        
        # Set up MPI execution
        mpi_info = MpiExecInfo(
            env=self.env,
            hostfile=self.jarvis.hostfile,
            nprocs=1,
            ppn=1  # Processes per node
        )
        
        # Execute the command
        self.exec_info = Exec(command, mpi_info)

    def stop(self):
        """
        Stop a running application. E.g., OrangeFS will terminate the servers,
        clients, and metadata services.
        :return: None
        """
        if hasattr(self, 'exec_info'):
            self.exec_info.wait()

    def kill(self):
        """
        Forcibly kill a running application. E.g., OrangeFS will terminate the servers,
        clients, and metadata services.
        :return: None
        """
        if hasattr(self, 'exec_info'):
            self.exec_info.kill()

    def clean(self):
        """
        Destroy all data for an application. E.g., OrangeFS will delete all
        metadata and data directories in addition to the orangefs.xml file.
        :return: None
        """
        # Clean up temporary directories
        import shutil
        try:
            shutil.rmtree('/tmp/test_data', ignore_errors=True)
            shutil.rmtree('/tmp/burst_buffer', ignore_errors=True) 
            shutil.rmtree('/tmp/pfs_storage', ignore_errors=True)
            shutil.rmtree('/tmp/final_output', ignore_errors=True)
        except:
            pass