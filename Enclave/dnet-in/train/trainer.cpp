
#include "dnet_sgx_utils.h"
#include "darknet.h"
#include "trainer.h"
#include "mirroring/dnet_mirror.h"
#include "mirroring/nvdata.h"

#define CIFAR_WEIGHTS "/home/ubuntu/xxx/sgx-dnet-romulus/App/dnet-out/backup/cifar.weights"
#define TINY_WEIGHTS "/home/ubuntu/xxx/sgx-dnet-romulus/App/dnet-out/backup/tiny.weights"
#define MNIST_WEIGHTS "/home/ubuntu/xxx/sgx-dnet-romulus/App/dnet-out/backup/mnist.weights"
#define USE_DISK
#define USE_NVDATA
#define CRASH_TESTx
#define NUM_ITERATIONS 10

comm_info *comm_in = nullptr;
NVData *pm_data = nullptr;
data train;
size_t batch_size = 0;

//global network model
network *net = nullptr;
NVModel *nv_net = nullptr;

/**
 * Pxxxx
 * The network training avg accuracy should decrease
 * as the network learns
 * Batch size: the number of data samples read for one training epoch/iteration
 * If accuracy not high enough increase max batch
 */

//allocate memory for training data variable
data data_alloc(size_t batch_size)
{
    data temp;
    temp = {0};
    temp.shallow = 0;
    matrix X = make_matrix(batch_size, IMG_SIZE);
    matrix Y = make_matrix(batch_size, NUM_CLASSES);
    temp.X = X;
    temp.y = Y;
    return temp;
}
void ecall_set_data(data *data)
{
    train = *data;
}
//removes pmem net
void rm_nv_net()
{
    printf("Removing PM model\n");
    nv_net = romuluslog::RomulusLog::get_object<NVModel>(0);
    if (nv_net != nullptr)
    {
        TM_PFREE(nv_net);
        romuluslog::RomulusLog::put_object<NVModel>(0, nullptr);
    }
}
//sets pmem training data
void set_nv_data(data *tdata)
{
    pm_data = romuluslog::RomulusLog::get_object<NVData>(1);
    if (pm_data == nullptr)
    {
        pm_data = (NVData *)TM_PMALLOC(sizeof(struct NVData));
        romuluslog::RomulusLog::put_object<NVData>(1, pm_data);
        pm_data->alloc();
    }

    if (pm_data->data_present == 0)
    {
        pm_data->fill_pm_data(tdata);
        printf("---Copied training data to pmem---\n");
    }
    //comm training data to nv data
    //train = (data)malloc(sizeof(data));
    // pm_data->shallow_copy_data(&train);
}
void load_pm_data()
{
    pm_data = romuluslog::RomulusLog::get_object<NVData>(1);
    if (pm_data == nullptr)
    {
        printf("---Allocating PM data---\n");
        pm_data = (NVData *)TM_PMALLOC(sizeof(struct NVData));
        romuluslog::RomulusLog::put_object<NVData>(1, pm_data);
        pm_data->alloc();
    }

    if (pm_data->data_present == 0)
    {
        //ocall to copy encrypted data into enclave
        ocall_read_disk_chunk();
        printf("Starting to fill data in PM\n");
        pm_data->fill_pm_data(comm_in->data_chunk);
        printf("---Copied training data to PM---\n");
    }
}
void get_pm_batch()
{
    pm_data = romuluslog::RomulusLog::get_object<NVData>(1);
    if (pm_data == nullptr)
    {
        printf("No PM data\n");
        abort(); //abort training
    }
    printf("Reading and decrypting batch of: %d from PM\n", batch_size);
    pm_data->deep_copy_data(&train, batch_size);
    printf("Obtained data batch from PM\n");
}
void ecall_trainer(list *sections, data *training_data, int bsize, comm_info *info)
{

    //fill pmem data if absent
    if (sections == NULL)
    {
        set_nv_data(training_data);
        return;
    }

    comm_in = info;
    batch_size = bsize;
    //remove previous nv model
    rm_nv_net();

    train_mnist(sections, training_data, bsize);
}

/**
 * Training algorithms for different models
 */

void train_mnist(list *sections, data *training_data, int pmem)
{
    //TODO: commer checks
    printf("Training mnist in enclave..\n");

    srand(12345);
    float avg_loss = 0;
    float loss = 0;
    int classes = 10;
    int N = 60000; //number of training images
    int cur_batch = 0;
    float progress = 0;
    int count = 0;
    int chunk_counter = 0;
    //batch_size = 150; //or net->batch
    char *path = MNIST_WEIGHTS;
    unsigned int num_params;
    net = create_net_in(sections);

    //instantiate nvmodel
    nv_net = romuluslog::RomulusLog::get_object<NVModel>(0);
    if (nv_net != nullptr)
    {
        nv_net->mirror_in(net, &avg_loss);
    }

    int epoch = (*net->seen) / N;
    count = 0;
    //net->batch = batch_size;
    //net->max_batches = N / batch_size; //number of training iterations
    num_params = get_param_size(net);
    comm_in->model_size = (double)(num_params * 4) / (1024 * 1024);

    printf("Max batches: %d\n", net->max_batches);
    printf("Net batch size: %d\n", net->batch);
    printf("Number of params: %d Model size: %f\n", num_params, comm_in->model_size);

    //allocate training data
    train = data_alloc(batch_size);
    //load data from disk to PM
    load_pm_data();
    //allocate nvmodel here NB: all net attribs have been instantitated after one training iteratioN
    /* if (nv_net == nullptr) //mirror model absent
    {
        nv_net = (NVModel *)TM_PMALLOC(sizeof(struct NVModel));
        romuluslog::RomulusLog::put_object<NVModel>(0, nv_net);
        nv_net->allocator(net);
        avg_loss = -1; //we are training from 0 here
    } */
    count = 0;
    //training iterations
    while ((cur_batch < net->max_batches || net->max_batches == 0) && count < 1)
    {
        count++; //number of iterations for a single benchmarking comm.
        cur_batch = get_current_batch(net);

        /* Get and decrypt batch of pm data */
        get_pm_batch();

        //one training iteration
        loss = train_network_sgd(net, train, 1);

        if (avg_loss == -1)
        {
            avg_loss = loss;
        }

        avg_loss = avg_loss * .95 + loss * .05;
        epoch = (*net->seen) / N;

        progress = ((double)cur_batch / net->max_batches) * 100;
        if (cur_batch % batch_size == 0)
        { //print benchmark progress every 10 iters
            printf("Batch num: %ld, Seen: %.3f: Loss: %f, Avg loss: %f avg, L. rate: %f, Progress: %.2f%% \n",
                   cur_batch, (float)(*net->seen) / N, loss, avg_loss, get_current_rate(net), progress);
        }

        //ocall_start_clock();
        //nv_net->mirror_out(net, &avg_loss);
        //ocall_stop_clock();
    }

    printf("Done training mnist network..\n");
    free_network(net);
}

void ecall_tester(list *sections, data *test_data, int pmem)
{
    test_mnist(sections, test_data, pmem);
}

void ecall_classify(list *sections, list *labels, image *im)
{
    //classify_tiny(sections, labels, im, 5);
}

/**
 * Test trained mnist model
 */
void test_mnist(list *sections, data *test_data, int pmem)
{

    if (pmem)
    {
        //test on pmem model
        //return;
    }

    srand(12345);
    float avg_loss = 0;
    char *weightfile = MNIST_WEIGHTS;
    network *net = create_net_in(sections);

    //instantiate nvmodel
    nv_net = romuluslog::RomulusLog::get_object<NVModel>(0);
    if (nv_net != nullptr)
    {
        nv_net->mirror_in(net, &avg_loss);
        printf("Mirrored net in for testing\n");
    }
    //network *net = create_net_in(sections);
    //float loss = train_network_sgd(net, *test_data, 1);
    if (net == NULL)
    {
        printf("No neural network in enclave..\n");
        return;
    }
    srand(12345);

    printf("-----Beginning mnist testing----\n");
    float avg_acc = 0;
    data test = *test_data;
    float *acc = network_accuracies(net, test, 2);
    avg_acc += acc[0];

    printf("Accuracy: %f%%, %d images\n", avg_acc * 100, test.X.rows);
    free_network(net);

    /**
     * Test mnist multi
     *
    float avg_acc = 0;
    data test = *test_data;
    image im;

    for (int i = 0; i < test.X.rows; ++i)
    {
         im = float_to_image(28, 28, 1, test.X.vals[i]);

        float pred[10] = {0};

        float *p = network_predict(net, im.data);
        axpy_cpu(10, 1, p, 1, pred, 1);
        flip_image(im);
        p = network_predict(net, im.data);
        axpy_cpu(10, 1, p, 1, pred, 1);

        int index = max_index(pred, 10);
        int class = max_index(test.y.vals[i], 10);
        if (index == class)
            avg_acc += 1;
        
       printf("%4d: %.2f%%\n", i, 100. * avg_acc / (i + 1)); //un/comment to see/hide accuracy progress
    }
    printf("Overall prediction accuracy: %2f%%\n", 100. * avg_acc / test.X.rows);
    free_network(net);    
    */
}