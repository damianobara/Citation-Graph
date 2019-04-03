#ifndef CG_CITATION_GRAPH_H
#define CG_CITATION_GRAPH_H

#include <map>
#include <memory>
#include <vector>
#include <exception>
#include <set>

class PublicationNotFound: public std::exception {

    virtual const char *what() const throw() {
        return "PublicationNotFound";
    }
};

class PublicationAlreadyCreated: public std::exception {

    virtual const char *what() const throw() {
        return "PublicationAlreadyCreated";
    }
};

class TriedToRemoveRoot: public std::exception {

    virtual const char* what() const throw() {
        return "TriedToRemoveRoot";
    }
};


template<class Publication>
class CitationGraph{
private:
    class Node;

    using idT = typename Publication::id_type;
    using mapT = std::map<idT, std::weak_ptr<Node>>;
    using mapShrPtr = std::shared_ptr<std::map<idT, std::weak_ptr<Node>>>;

    mapShrPtr mapGraph;
    std::shared_ptr<Node> root;

    class Node {
    public:
        Publication pub;
        std::set<std::shared_ptr<Node>, std::owner_less<std::shared_ptr<Node>>> children;
        std::set<std::weak_ptr<Node>, std::owner_less<std::weak_ptr<Node>>> parents;
        typename mapT::iterator it;
        mapShrPtr mapNode; //Potrzebna do usuwania się z mapy w destruktorze.
        bool setedIterator;//Potrzebny do destruktora, który nie rzuca wyjątków.


        Node(idT const &stem_id, mapShrPtr map) : pub(stem_id), setedIterator(false) {
            mapNode = map;
            it = map->begin();
        }

        ~Node() { deleteYourselfFromMap(); }

        void setIterator(typename mapT::iterator&& myPlace){
            it = myPlace;
            setedIterator = true;
        }
    private:
        //Jest noexcept ponieważ usuwam się iteratorem.
        void deleteYourselfFromMap() noexcept { if(setedIterator) mapNode->erase(it); }

    };

public:
    CitationGraph(idT const &stem_id): mapGraph(new mapT()){

        root = std::shared_ptr<Node>(new Node(stem_id, mapGraph));

        try {
            mapGraph->insert(std::make_pair(stem_id, root));
            root->setIterator( mapGraph->find(stem_id) );

        } catch(...) {
            root.reset();
            throw;
        }

    }


    CitationGraph<Publication>& operator=(CitationGraph<Publication> &&other) noexcept {

        if (this == &other) return *this;
        root = std::move(other.root);
        mapGraph = other.mapGraph;
        return *this;
    }

    CitationGraph(CitationGraph<Publication> &&other) noexcept {
        root = other.root;
        mapGraph = other.mapGraph;
    }

    idT get_root_id() const noexcept(noexcept(std::declval<Publication>().get_id())) {
        return root->pub.get_id();
    }

    std::vector<idT> get_children(idT const &id) const {

        if(mapGraph->find(id) == mapGraph->end())
            throw PublicationNotFound();

        else {
            std::shared_ptr<Node> node(mapGraph->at(id));

            std::vector<idT> ids;

            for (auto &child: node->children) {
                ids.push_back(child->pub.get_id());
            }

            return ids;
        }
    }

    std::vector<idT> get_parents(idT const &id) const {

        if(mapGraph->find(id) == mapGraph->end()) {
            throw PublicationNotFound();
        }

        else {
            std::weak_ptr<Node> node = mapGraph->at(id);
            std::vector<idT> ids;
            for (auto &parent: node.lock()->parents) {

                if(parent.lock() == nullptr)
                    node.lock()->parents.erase(parent);
                else
                    ids.push_back(parent.lock()->pub.get_id());
            }
            return ids;
        }
    }

    bool exists(typename Publication::id_type const &id) const {
        return (mapGraph->find(id) != mapGraph->end());
    }

    Publication& operator[](idT const &id) const {
        if (!exists(id)) {
            throw PublicationNotFound();
        }
        auto ptr = mapGraph->at(id);
        return ptr.lock()->pub;

    }

    void create(idT const &id, std::vector<idT> const &parent_ids) {

        if (exists(id)) {
            throw PublicationAlreadyCreated();
        }
        for (auto p: parent_ids) {
            if (!exists(p)) {
                throw PublicationNotFound();
            }
        }

        std::shared_ptr<Node> node(new Node(id, mapGraph));
        std::vector<std::weak_ptr<Node>> parentPtrs;

        for (auto &parent: parent_ids) {
            //Tworzę wektor wskaznikow na ojców, żeby później nie było wyjątków podczas ustawiania się jako ich dziecko
            parentPtrs.emplace_back( std::weak_ptr<Node>( mapGraph->at(parent).lock() ) );
            node->parents.insert(std::weak_ptr<Node>(mapGraph->at(parent)));
        }

        mapGraph->insert(std::make_pair(id, node));
        node->setIterator(mapGraph->find(id));


        //Do tej pory jak pojawi się wyjątek nic nie robimy bo nie modyfikowaliśmy struktury grafu.
        try {
            for (auto &ptr: parentPtrs)
                ptr.lock()->children.insert(std::shared_ptr<Node>(node));
                //Coś poszło nie tak przy dodawaniu dziecka ojcu
        } catch(...) {
            //Back_crawl dla wszystkich ojców iterujemy się po secie i patrzymy czy jest
            //jest dodany syn (iterujemy się bo dzięki temu nie dostaniemy wyjątku który
            //mógłby być przy find. Nie usuwam z mapy bo tam i tak siedzi weka_ptr który
            //zniknie po usunięciu ostatniego shared_ptr z ojców.
            for (auto &ptr: parentPtrs){

                for( auto &son: ptr.lock()->children) {

                    if (son == node) {
                        ptr.lock()->children.erase(son);
                        break;
                    }
                }
            }
            throw;
        }

    }

    void create(idT const &id, idT const &parent_id) {
        std::vector<idT> v(1, parent_id);
        create(id, v);
    }

    void add_citation(idT const &child_id, idT const &parent_id) {
        if (!exists(child_id)) throw PublicationNotFound();
        if (!exists(parent_id)) throw PublicationNotFound();

        std::shared_ptr<Node> child(mapGraph->at(child_id));
        std::weak_ptr<Node> parent(mapGraph->at(parent_id));

        child->parents.insert(parent);
        try {
            parent.lock()->children.insert(child);
        } catch(...) {
            //Usunięcie ojca z listy ojców
            for(auto father: child->parents){

                if( father.lock() == parent.lock() ) {
                    //noexcept bo usuwam iteratorem
                    child->parents.erase(father);
                    break;
                }

            }
            throw;
        }
    }

    void remove(idT const &id) {

        if (!exists(id)) throw PublicationNotFound();

        std::shared_ptr<Node> node(mapGraph->at(id));

        if (node == root) throw TriedToRemoveRoot();

        std::vector<std::shared_ptr<Node>> backUp;
        //Usuwam się tylko z dzieci moich ojców bo tam są shered_pointery
        //na koniec usuwam się z mapy poprzez iterator node`a żeby nie
        //było tam jakiegoś zombie (w sensie klucz na usunięty weak_ptr)
        try {
            for (auto &parent: node->parents) {

                auto set_iterator = parent.lock()->children.begin();
                while ( (*set_iterator) != node) {
                    set_iterator++;
                }
                backUp.emplace_back( (*set_iterator) );
                parent.lock()->children.erase( (*set_iterator) );
            }

            //Sytuacja gdy coś poszło nie tak z usuwaniem się z dzieci u jakiegoś ojca node`a
        } catch(...) {

            for(auto father: backUp)
                father->children.insert(node);

            backUp.clear();

            throw;
        }

        backUp.clear();
    }

};

#endif //CG_CITATION_GRAPH_H