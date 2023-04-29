import os
import numpy as np
import math
from pathlib import Path
from scipy.io import mmread
from scipy.io import mmwrite
from io import BytesIO
import sys

def generate_dist_matrix(theta, sigma, network_size):
    return np.array([np.random.normal(loc = theta, scale = sigma, size = 100) for x in range(network_size)])

theta = float(sys.argv[1])
sigma = float(sys.argv[2])
network_size = int(sys.argv[3])
name_of_system = sys.argv[4]
system_hold_folder = sys.argv[5]

distributions = generate_dist_matrix(theta, sigma, network_size)
mmwrite(system_hold_folder + "/dist_matrix.mtx", distributions)




