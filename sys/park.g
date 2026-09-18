G90
G1 Z{min(move.axes[2].machinePosition + 5, move.axes[2].max)} F300
G1 X{move.axes[0].max} Y{move.axes[1].max} F1500
