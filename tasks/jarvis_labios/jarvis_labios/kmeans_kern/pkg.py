"""
This module provides classes and methods to launch the KmeansKern application.
KmeansKern is ....
"""
from jarvis_cd.basic.pkg import Application
from jarvis_util import *
import os

class KmeansKern(Application):
    """
    This class provides methods to launch the KmeansKern application.
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
        return [
            {
                'name': 'nprocs',
                'msg': 'Number of processes',
                'type': int,
                'default': 1,
            },
            {
                'name': 'ppn',
                'msg': 'The number of processes per node',
                'type': int,
                'default': 1,
            },
            {
                'name': 'mode',
                'msg': 'The mode to run in',
                'type': str,
                'choices': ['base', 'labios'],
                'default': 'base',
            },
            {
                'name': 'file_path',
                'msg': 'The path to the per-process output file directory',
                'type': str,
                'default': '/tmp/kmeans_kern/',
            },
            {
                'name': 'pfs_path',
                'msg': 'The path to the final output file directory',
                'type': str,
                'default': '/tmp/kmeans_kern_pfs/',
            },
            {
                'name': 'iterations',
                'msg': 'The number of iterations to run',
                'type': int,
                'default': 1,
            }
        ]

    def _configure(self, **kwargs):
        """
        Converts the Jarvis configuration to application-specific configuration.
        E.g., OrangeFS produces an orangefs.xml file.

        :param kwargs: Configuration parameters for this pkg.
        :return: None
        """
        if self.config['file_path'][-1] != '/':
            self.config['file_path'] += '/'
        if self.config['pfs_path'][-1] != '/':
            self.config['pfs_path'] += '/'
        self.config['final_output_file'] = f"{self.config['pfs_path']}final.dat"
        Mkdir(self.config['file_path'], PsshExecInfo(hostfile=self.jarvis.hostfile))
        Mkdir(self.config['pfs_path'], PsshExecInfo(hostfile=self.jarvis.hostfile))

    def start(self):
        """
        Launch an application. E.g., OrangeFS will launch the servers, clients,
        and metadata services on all necessary pkgs.

        :return: None
        """
        if self.config['mode'] == 'base':
            exec_path = 'kmeans_base'
        else:
            exec_path = 'kmeans_tabios'
        
        cmd = [
            exec_path,
            self.config['file_path'],
            str(self.config['iterations']),
            self.config['pfs_path']
        ]

        self.exec_info = Exec(' '.join(cmd),
            MpiExecInfo(env=self.mod_env,
                         hostfile=self.jarvis.hostfile,
                         nprocs=self.config['nprocs'],
                         ppn=self.config['ppn']))

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
        Rm(f"{self.config['file_path']}/*", PsshExecInfo(hostfile=self.jarvis.hostfile))
        Rm(self.config['file_path'], PsshExecInfo(hostfile=self.jarvis.hostfile))
        Rm(self.config['final_output_file'], PsshExecInfo(hostfile=self.jarvis.hostfile))
        Rm(self.config['pfs_path'], PsshExecInfo(hostfile=self.jarvis.hostfile)) 