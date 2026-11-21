//
//  BuildCatalogView.swift
//  realms iOS
//

import UIKit

struct SandboxCatalogEntry {
    let nativeType: Int32
    let label: String
    let asset: String
    let defaultSurface: Int
}

enum SandboxBuildCatalog {
    static let entries: [SandboxCatalogEntry] = [
        SandboxCatalogEntry(nativeType: 0,  label: loc("item_concrete_block"),     asset: "catalog/rendered/wall.webp",    defaultSurface: 1),
        SandboxCatalogEntry(nativeType: 1,  label: loc("item_floor_slab"),       asset: "catalog/rendered/floor.webp",   defaultSurface: 1),
        SandboxCatalogEntry(nativeType: 2,  label: loc("item_window"),              asset: "catalog/rendered/window.webp",  defaultSurface: 1),
        SandboxCatalogEntry(nativeType: 3,  label: loc("item_door"),             asset: "catalog/rendered/door.webp",    defaultSurface: 5),
        SandboxCatalogEntry(nativeType: 4,  label: loc("item_table"),              asset: "catalog/rendered/table.webp",   defaultSurface: 5),
        SandboxCatalogEntry(nativeType: 5,  label: loc("item_chair"),              asset: "catalog/rendered/chair.webp",   defaultSurface: 5),
        SandboxCatalogEntry(nativeType: 6,  label: loc("item_cabinet"),              asset: "catalog/rendered/cabinet.webp", defaultSurface: 5),
        SandboxCatalogEntry(nativeType: 7,  label: loc("item_sofa"),             asset: "catalog/rendered/sofa.webp",    defaultSurface: 6),
        SandboxCatalogEntry(nativeType: 9,  label: loc("item_wood_block"),   asset: "textures/wood.png",             defaultSurface: 5),
        SandboxCatalogEntry(nativeType: 10, label: loc("item_brick_block"),    asset: "textures/brick.png",            defaultSurface: 2),
        SandboxCatalogEntry(nativeType: 11, label: loc("item_metal_block"), asset: "textures/rust.png",            defaultSurface: 3),
        SandboxCatalogEntry(nativeType: 8,  label: loc("item_uaz"),           asset: "catalog/rendered/uaz.webp",     defaultSurface: -1),
        SandboxCatalogEntry(nativeType: 12, label: loc("item_cube"),         asset: "textures/concrete.png",         defaultSurface: 1)
    ]

    static func indices(forCategory category: Int) -> [Int] {
        entries.indices.filter { idx in
            let t = entries[idx].nativeType
            switch category {
            case 0: return !(4...7).contains(t)
            case 1: return (4...7).contains(t)
            default: return false
            }
        }
    }

    static func loadBundleImage(_ relativePath: String) -> UIImage? {
        guard let root = Bundle.main.resourcePath else { return nil }
        let fullPath = (root as NSString).appendingPathComponent(relativePath)
        return UIImage(contentsOfFile: fullPath)
    }
}

/// Isometric material block preview matching Android's MaterialBlockView.
final class MaterialSwatchView: UIView {
    var materialId: Int {
        didSet { setNeedsDisplay() }
    }
    var textureImage: UIImage? {
        didSet { setNeedsDisplay() }
    }

    init(materialId: Int) {
        self.materialId = materialId
        super.init(frame: .zero)
        backgroundColor = .clear
        isOpaque = false
        isUserInteractionEnabled = false
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext() else { return }
        let size = min(bounds.width, bounds.height)
        guard size > 4 else { return }

        ctx.saveGState()
        ctx.translateBy(x: (bounds.width - size) * 0.5, y: (bounds.height - size) * 0.5)
        ctx.scaleBy(x: size / 100.0, y: size / 100.0)

        let baseColor: UIColor
        switch materialId {
        case 5: baseColor = UIColor(red: 0.61, green: 0.43, blue: 0.23, alpha: 1.0)
        case 2: baseColor = UIColor(red: 0.67, green: 0.35, blue: 0.28, alpha: 1.0)
        case 3: baseColor = UIColor(red: 0.40, green: 0.47, blue: 0.51, alpha: 1.0)
        case 6: baseColor = UIColor(red: 0.25, green: 0.34, blue: 0.29, alpha: 1.0)
        default: baseColor = UIColor(red: 0.65, green: 0.67, blue: 0.67, alpha: 1.0)
        }

        func drawFace(_ pts: [(CGFloat, CGFloat)], shadeAlpha: CGFloat) {
            let path = UIBezierPath()
            path.move(to: CGPoint(x: pts[0].0, y: pts[0].1))
            for i in 1..<pts.count {
                path.addLine(to: CGPoint(x: pts[i].0, y: pts[i].1))
            }
            path.close()

            ctx.saveGState()
            ctx.addPath(path.cgPath)
            ctx.clip()

            ctx.setFillColor(baseColor.cgColor)
            ctx.fill(CGRect(x: 0, y: 0, width: 100, height: 100))

            if let img = textureImage {
                img.draw(in: path.bounds, blendMode: .normal, alpha: 0.85)
            }

            if shadeAlpha > 0 {
                ctx.setFillColor(UIColor(white: 0.0, alpha: shadeAlpha).cgColor)
                ctx.fill(CGRect(x: 0, y: 0, width: 100, height: 100))
            }
            ctx.restoreGState()

            ctx.setStrokeColor(UIColor(white: 1.0, alpha: 0.55).cgColor)
            ctx.setLineWidth(1.2)
            ctx.addPath(path.cgPath)
            ctx.strokePath()
        }

        drawFace([(50, 6), (90, 27), (50, 49), (10, 27)], shadeAlpha: 0.0)
        drawFace([(10, 27), (50, 49), (50, 94), (10, 72)], shadeAlpha: 0.14)
        drawFace([(50, 49), (90, 27), (90, 72), (50, 94)], shadeAlpha: 0.28)

        ctx.restoreGState()
    }
}

/// 4-tab Russian Build & FPV Catalog popup panel matching Android's showBuildCatalog().
final class BuildCatalogPopupView: UIView {
    var selectedCatalogIndex: Int = 0 {
        didSet { rebuildContent() }
    }
    var onSelectCatalogItem: ((Int) -> Void)?
    var onTakeFpvRemote: (() -> Void)?
    var onTakeFpvDrone: (() -> Void)?
    var onClose: (() -> Void)?

    private var currentCategory: Int = 0
    private let headerLabel = UILabel()
    private let closeButton = UIButton(type: .system)
    private let tabStack = UIStackView()
    private let contentArea = UIView()
    private var tabButtons: [UIButton] = []

    private var cachedItemImages: [Int: UIImage] = [:]
    private var cachedMaterialImages: [Int: UIImage] = [:]
    private var cachedExtraImages: [String: UIImage] = [:]

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = UIColor(red: 0.125, green: 0.169, blue: 0.20, alpha: 0.93)
        layer.cornerRadius = 12
        layer.borderWidth = 1
        layer.borderColor = UIColor(red: 0.49, green: 0.67, blue: 0.65, alpha: 0.55).cgColor
        clipsToBounds = true

        preloadAssets()
        setupChrome()
        rebuildContent()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func preloadAssets() {
        for (idx, entry) in SandboxBuildCatalog.entries.enumerated() {
            if let img = SandboxBuildCatalog.loadBundleImage(entry.asset) {
                cachedItemImages[idx] = img
            }
        }
        let matMap: [Int: String] = [
            5: "textures/wood.png",
            1: "textures/concrete.png",
            2: "textures/brick.png",
            3: "textures/rust.png",
            6: "textures/fabric.png"
        ]
        for (matId, path) in matMap {
            if let img = SandboxBuildCatalog.loadBundleImage(path) {
                cachedMaterialImages[matId] = img
            }
        }
        for extra in ["catalog/rendered/fpv_remote.webp", "catalog/rendered/fpv_drone.webp"] {
            if let img = SandboxBuildCatalog.loadBundleImage(extra) {
                cachedExtraImages[extra] = img
            }
        }
    }

    func itemImage(at index: Int) -> UIImage? {
        cachedItemImages[index]
    }

    func materialImage(for materialId: Int) -> UIImage? {
        cachedMaterialImages[materialId]
    }

    private func setupChrome() {
        headerLabel.text = loc("catalog_title")
        headerLabel.font = UIFont.systemFont(ofSize: 14, weight: .bold)
        headerLabel.textColor = .white
        addSubview(headerLabel)

        closeButton.setTitle("×", for: .normal)
        closeButton.titleLabel?.font = UIFont.systemFont(ofSize: 24, weight: .semibold)
        closeButton.setTitleColor(.white, for: .normal)
        closeButton.accessibilityLabel = loc("catalog_close")
        closeButton.addTarget(self, action: #selector(handleClose), for: .touchUpInside)
        addSubview(closeButton)

        tabStack.axis = .horizontal
        tabStack.distribution = .fillEqually
        tabStack.spacing = 4
        let titles = [loc("catalog_tab_construction"), loc("catalog_tab_furniture"), loc("catalog_tab_weapons"), loc("catalog_tab_fpv")]
        for (idx, title) in titles.enumerated() {
            let btn = UIButton(type: .system)
            btn.tag = idx
            btn.setTitle(title, for: .normal)
            btn.titleLabel?.font = UIFont.systemFont(ofSize: 11, weight: .semibold)
            btn.layer.cornerRadius = 6
            btn.addTarget(self, action: #selector(handleTabTap(_:)), for: .touchUpInside)
            tabButtons.append(btn)
            tabStack.addArrangedSubview(btn)
        }
        addSubview(tabStack)
        addSubview(contentArea)
        updateTabStyles()
    }

    private func updateTabStyles() {
        for (idx, btn) in tabButtons.enumerated() {
            let active = (idx == currentCategory)
            btn.backgroundColor = active
                ? UIColor(red: 0.22, green: 0.36, blue: 0.34, alpha: 0.95)
                : UIColor(white: 1.0, alpha: 0.08)
            btn.setTitleColor(active
                ? UIColor(red: 0.49, green: 1.0, blue: 0.82, alpha: 1.0)
                : .white, for: .normal)
        }
    }

    @objc private func handleClose() {
        onClose?()
    }

    @objc private func handleTabTap(_ sender: UIButton) {
        currentCategory = sender.tag
        updateTabStyles()
        rebuildContent()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        let pad: CGFloat = 8
        headerLabel.frame = CGRect(x: pad + 4, y: 4, width: bounds.width - 64, height: 30)
        closeButton.frame = CGRect(x: bounds.width - 48, y: 2, width: 44, height: 32)
        tabStack.frame = CGRect(x: pad, y: 36, width: bounds.width - pad * 2, height: 34)
        contentArea.frame = CGRect(x: pad, y: 76, width: bounds.width - pad * 2, height: max(0, bounds.height - 84))
        layoutContentArea()
    }

    private var activeGridScroll: UIScrollView?
    private var activeGridCells: [UIView] = []
    private var activeFpvStack: UIStackView?
    private var activeEmptyLabel: UILabel?

    private func rebuildContent() {
        contentArea.subviews.forEach { $0.removeFromSuperview() }
        activeGridScroll = nil
        activeGridCells.removeAll()
        activeFpvStack = nil
        activeEmptyLabel = nil

        if currentCategory == 3 {
            let stack = UIStackView()
            stack.axis = .horizontal
            stack.distribution = .fillEqually
            stack.spacing = 8

            stack.addArrangedSubview(makeFpvCard(title: loc("catalog_card_remote"),
                                                 asset: "catalog/rendered/fpv_remote.webp",
                                                 action: #selector(handleTakeRemote)))
            stack.addArrangedSubview(makeFpvCard(title: loc("catalog_card_drone"),
                                                 asset: "catalog/rendered/fpv_drone.webp",
                                                 action: #selector(handleTakeDrone)))
            contentArea.addSubview(stack)
            activeFpvStack = stack
            setNeedsLayout()
            return
        }

        let visibleIndices = SandboxBuildCatalog.indices(forCategory: currentCategory)
        if visibleIndices.isEmpty {
            let empty = UILabel()
            empty.text = loc("catalog_empty")
            empty.textColor = UIColor(white: 0.88, alpha: 1.0)
            empty.font = UIFont.systemFont(ofSize: 14, weight: .medium)
            empty.textAlignment = .center
            contentArea.addSubview(empty)
            activeEmptyLabel = empty
            setNeedsLayout()
            return
        }

        let scroll = UIScrollView()
        scroll.showsVerticalScrollIndicator = true
        contentArea.addSubview(scroll)
        activeGridScroll = scroll

        for catalogIndex in visibleIndices {
            let cell = makeItemCell(catalogIndex: catalogIndex)
            scroll.addSubview(cell)
            activeGridCells.append(cell)
        }
        setNeedsLayout()
    }

    private func makeFpvCard(title: String, asset: String, action: Selector) -> UIControl {
        let card = UIControl()
        card.backgroundColor = UIColor(red: 0.23, green: 0.27, blue: 0.32, alpha: 0.45)
        card.layer.cornerRadius = 8
        card.layer.borderWidth = 1
        card.layer.borderColor = UIColor(white: 1.0, alpha: 0.18).cgColor
        card.addTarget(self, action: action, for: .touchUpInside)

        let imgView = UIImageView(image: cachedExtraImages[asset])
        imgView.contentMode = .scaleAspectFit
        imgView.translatesAutoresizingMaskIntoConstraints = false
        imgView.isUserInteractionEnabled = false
        card.addSubview(imgView)

        let lbl = UILabel()
        lbl.text = title
        lbl.textColor = .white
        lbl.font = UIFont.systemFont(ofSize: 12, weight: .semibold)
        lbl.textAlignment = .center
        lbl.translatesAutoresizingMaskIntoConstraints = false
        lbl.isUserInteractionEnabled = false
        card.addSubview(lbl)

        NSLayoutConstraint.activate([
            imgView.topAnchor.constraint(equalTo: card.topAnchor, constant: 8),
            imgView.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: 8),
            imgView.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -8),
            imgView.bottomAnchor.constraint(equalTo: lbl.topAnchor, constant: -4),
            lbl.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: 4),
            lbl.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -4),
            lbl.bottomAnchor.constraint(equalTo: card.bottomAnchor, constant: -6),
            lbl.heightAnchor.constraint(equalToConstant: 28)
        ])
        return card
    }

    private func makeItemCell(catalogIndex: Int) -> UIControl {
        let entry = SandboxBuildCatalog.entries[catalogIndex]
        let isSelected = (catalogIndex == selectedCatalogIndex)

        let cell = UIControl()
        cell.tag = catalogIndex
        cell.backgroundColor = isSelected
            ? UIColor(red: 0.28, green: 0.45, blue: 0.39, alpha: 0.72)
            : UIColor(red: 0.23, green: 0.27, blue: 0.32, alpha: 0.38)
        cell.layer.cornerRadius = 6
        cell.layer.borderWidth = isSelected ? 1.2 : 0.0
        cell.layer.borderColor = UIColor(red: 0.57, green: 0.85, blue: 0.67, alpha: 1.0).cgColor
        cell.addTarget(self, action: #selector(handleSelectCell(_:)), for: .touchUpInside)

        let isMaterialBlock = [0, 9, 10, 11, 12].contains(entry.nativeType)
        let previewView: UIView
        if isMaterialBlock {
            let swatch = MaterialSwatchView(materialId: entry.defaultSurface)
            swatch.textureImage = cachedMaterialImages[entry.defaultSurface]
            previewView = swatch
        } else {
            let iv = UIImageView(image: cachedItemImages[catalogIndex])
            iv.contentMode = .scaleAspectFit
            previewView = iv
        }
        previewView.isUserInteractionEnabled = false
        previewView.translatesAutoresizingMaskIntoConstraints = false
        cell.addSubview(previewView)

        let lbl = UILabel()
        lbl.text = entry.label
        lbl.textColor = .white
        lbl.font = UIFont.systemFont(ofSize: 10, weight: .medium)
        lbl.textAlignment = .center
        lbl.lineBreakMode = .byTruncatingTail
        lbl.isUserInteractionEnabled = false
        lbl.translatesAutoresizingMaskIntoConstraints = false
        cell.addSubview(lbl)

        NSLayoutConstraint.activate([
            previewView.topAnchor.constraint(equalTo: cell.topAnchor, constant: 4),
            previewView.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 4),
            previewView.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -4),
            previewView.bottomAnchor.constraint(equalTo: lbl.topAnchor, constant: -2),
            lbl.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 2),
            lbl.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -2),
            lbl.bottomAnchor.constraint(equalTo: cell.bottomAnchor, constant: -3),
            lbl.heightAnchor.constraint(equalToConstant: 18)
        ])

        return cell
    }

    private func layoutContentArea() {
        let w = contentArea.bounds.width
        let h = contentArea.bounds.height
        activeFpvStack?.frame = contentArea.bounds
        activeEmptyLabel?.frame = contentArea.bounds

        if let scroll = activeGridScroll {
            scroll.frame = contentArea.bounds
            let cols = 4
            let spacing: CGFloat = 5
            let cellW = max(54, (w - spacing * CGFloat(cols - 1)) / CGFloat(cols))
            let cellH: CGFloat = 82
            for (i, cell) in activeGridCells.enumerated() {
                let col = i % cols
                let row = i / cols
                cell.frame = CGRect(x: CGFloat(col) * (cellW + spacing),
                                    y: CGFloat(row) * (cellH + spacing),
                                    width: cellW,
                                    height: cellH)
            }
            let totalRows = (activeGridCells.count + cols - 1) / cols
            scroll.contentSize = CGSize(width: w, height: max(h, CGFloat(totalRows) * (cellH + spacing)))
        }
    }

    @objc private func handleSelectCell(_ sender: UIControl) {
        onSelectCatalogItem?(sender.tag)
    }

    @objc private func handleTakeRemote() {
        onTakeFpvRemote?()
    }

    @objc private func handleTakeDrone() {
        onTakeFpvDrone?()
    }
}
