import sys

# Generates new config file 
def generate_machines():
    config = open("config.cfg", "w")
    # Percentage of neighbors must be between .1 and 1 inclusive
    if (float(sys.argv[2]) < 0.1) or (float(sys.argv[2]) > 1.0):
        print('Alpha must be between 0.1 and 1 inclusive')
        return
    # Follows the format used by config file parsers in cpp code
    for i in range(1, int(sys.argv[1]) + 1):
        config.writelines("machine{}\n".format(i))
        config.writelines("127.0.0.1\n")
        config.writelines("{}\n".format(1000 + ((i-1)* 100)))
        config.writelines("tag {}\n".format(i))
        config.writelines("-\n")
        bound = int(float(sys.argv[2])*int(sys.argv[1]))
        close_neighbors = range(i-bound,i+bound+1)
        possible_subscription = [x for x in close_neighbors if x > 0 and x <= int(sys.argv[1])]
        for x in possible_subscription:
            if x != i:
                config.writelines("tag {}\n".format(x))
        config.writelines("-\n")
        if i < int(sys.argv[1]):
            config.writelines("machine{}\n".format(i+1))
        config.writelines("---\n")
    config.close()

generate_machines()